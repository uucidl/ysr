// Parse makefiles usually consumed by the ysr script, in order
// to validate and transform them into other forms. This is part
// of an exploration to move away from make/ysr into other build
// systems.
//
// 2021-12-14 -Nicolas Léveillé

//
// So make appears to be both the dependency engine, with some kind
// of macro-expansion pre-processor bolted onto it. I should never
// have built anything substantial onto that.
//

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "wyhash.h"

static int emit_debug_log = 0;

typedef char const *lstr;

typedef struct BufHeader {
    int size;
    int capacity;
} BufHeader;

#define anew(hdr, data_ptr) do { \
    hdr = (BufHeader) { 0 , }; \
    data_ptr = 0; \
} while (0)

#define afree(hdr, data_ptr) do { \
    hdr = (BufHeader) { 0, }; \
    free(data_ptr); \
    data_ptr = 0; \
} while (0)

#define agrow(hdr, data_ptr, n) do { \
    int new_capacity = buf_fit_capacity(hdr, n); \
    data_ptr = recallocz(data_ptr, hdr.capacity, new_capacity, sizeof *data_ptr); \
    hdr.capacity = new_capacity; \
} while (0)

typedef struct Charbuf {
    BufHeader header;
    char *data;
} Charbuf;

typedef struct FixedSizeArena {
    Charbuf memory;
} FixedSizeArena;

typedef struct Project {
    char const *topdir /* TOP variable */;
    char const *projectfile; /* YSR.project.file variable */
    char const *ysrlibdir; /* YSR.libdir variable */
    char const *host_config_mk; /* HOST_CONFIG_MK variable */
} Project;

typedef struct Lexer {
    // input data:
    lstr input;
    int endpos;

    int pos;
    int logical_line;
    char recipe_prefix_char; // by default \t but can be changed with .RECIPEPREFIX

    int num_tokens; // stats
} Lexer;

typedef enum TokenKind {
    TokenKind_None = 0,
    TokenKind_Assignment,
    TokenKind_Eol,
    TokenKind_Escape,
    TokenKind_Recipe,
} TokenKind;

typedef struct Token {
    int pos, len;
    char kind;
} Token;


typedef struct Module {
    lstr name;
    int is_confirmed_to_be_a_module;
} Module;

typedef struct Build {
    FixedSizeArena arena;
    BufHeader modules_header;
    Module *modules;
    uint64_t *module_name_hashes;
} Build;


// You can't really parse and lex makefiles without also interpreting them, since variables definitions have direct influences on
typedef struct Interpreter {
    Lexer *lexer;
    char* filename;
    Charbuf tmpbuf;

    struct {
        int n;
        int cap;
        char **names;
        int *names_len;
        char **values;
        char *is_recursive;
    } variables;

    Build *build; // output of our interpreter.
} Interpreter;

uint64_t
hash(char const *bytes, size_t n) {
    return wyhash(bytes, n, 0, _wyp);
}

int align_up(int size, int alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

int greater_of(int a, int b) {
    return a > b ? a : b;
}

void* reallocz(void *ptr, size_t old_size, size_t size) {
    if (old_size != 0) {
        char *bytes = ptr;
        assert(((unsigned char)bytes[old_size]) == 0xfe);
    }
    ptr = realloc(ptr, size + 1);
    if (size > old_size) {
        char *bytes = ptr;
        memset(&bytes[old_size], 0, size - old_size);
    }
    char *bytes = ptr;
    bytes[size] = 0xfe;

    return ptr;
}

void* recallocz(void *ptr, size_t old_num, size_t new_num, size_t elem_size) {
    return reallocz(ptr, old_num * elem_size, new_num * elem_size);
}

void
buf_reset(BufHeader *buf) {
    buf->size = 0;
}

void
chars_free(Charbuf *buf) {
    buf_reset(&buf->header);
    free(buf->data);
    buf->header = (BufHeader){ 0 , };
}

// returns 1 if it needs a realloc for n additional bytes
int
buf_wouldgrow(BufHeader *buf, int n) {
    return buf->capacity - buf->size < n;
}

int
buf_fit_capacity(BufHeader buf, int n) {
    return align_up(greater_of(2*buf.capacity, buf.size + n), 4096);
}

void
chars_reserve(Charbuf *charbuf, size_t new_capacity) {
    BufHeader *buf = &charbuf->header;
    assert(new_capacity >= (size_t)buf->size);
    charbuf->data = reallocz(charbuf->data, buf->capacity, new_capacity);
    buf->capacity = new_capacity;
}

void
chars_push_nstr(Charbuf *chars, size_t n, lstr str) {
    if (n == 0)
        return;
    BufHeader *buf = (BufHeader*)chars;
    int needed_n = n + 1 /* implicit zero terminator */;
    if (buf_wouldgrow(buf, needed_n)) {
        int new_capacity = buf_fit_capacity(*buf, needed_n);
        chars_reserve(chars, new_capacity);
    }
    memcpy(&chars->data[buf->size], &str[0], n);
    chars->data[buf->size + n] = '\0'; // always null terminate the strings for compat with C
    buf->size += n;
}

void
arena_create(FixedSizeArena *arena, int size) {
    chars_reserve(&arena->memory, size);
}

void* arena_alloc(int size, FixedSizeArena *arena) {
    if (buf_wouldgrow(&arena->memory.header, size)) {
        return 0;
    }

    void *p = &arena->memory.data[arena->memory.header.size];
    arena->memory.header.size += size;

    return p;
}

void arena_free(FixedSizeArena *arena) {
    chars_free(&arena->memory);
}

static char *
read_whole_file(lstr const filename, size_t *num_bytes_ptr, char ** err_ptr) {
    char *result = 0;
    char *buffer = 0;
    int errc = 0;

    *num_bytes_ptr = 0;
    *err_ptr = 0;

    FILE *file = fopen(filename, "rb");
    if (!file) {
        *err_ptr = "could not open file with fopen";
        perror(*err_ptr);
        return 0;
    }

    errc = fseek(file, 0, SEEK_END);
    if (errc) {
        *err_ptr = "could not seek to the end of the file with fseek";
        perror(*err_ptr);
        goto return_with_file_open;
    }

    int num_bytes_or_error_if_negative = ftell(file);
    if (num_bytes_or_error_if_negative < 0) {
        *err_ptr = "could not get the size of the file with ftell";
        perror(*err_ptr);
        goto return_with_file_open;
    }

    int num_bytes = num_bytes_or_error_if_negative;

    buffer = calloc(num_bytes + 1 /* null terminator */, 1);
    errc = fseek(file, 0, SEEK_SET); // rewind to beginning.
    if (errc) {
        *err_ptr = "could not rewind to beginning of file";
        perror(*err_ptr);
        goto return_with_file_open;
    }
    if (fread(buffer, num_bytes, 1, file) < 1) {
        if (feof(file)) {
            *err_ptr = "encountered end of file during fread while reading all the bytes";
            goto return_with_file_open;
        } else {
            *err_ptr = "could not read entire file with fread";
            perror(*err_ptr);
        }
        goto return_with_file_open;
    }
    buffer[num_bytes] = 0; // 0 terminator to help with parsing.

    result = buffer; buffer = 0; // success!
    *num_bytes_ptr = num_bytes;

    return_with_file_open:
        free(buffer);
    (void) fclose(file); // since we're only reading the file we can ignore errors on fclose.

    return result;
}


Token
eof_token(Lexer *lexer) {
    return (struct Token) { .pos = lexer->endpos };
}

void
consume_whitespace(Lexer *lexer) {
    lstr p = lexer->input;
    while (p[lexer->pos] == ' ' || p[lexer->pos] == '\t') {
        lexer->pos++;
    }
}

int
is_word_at_char(char c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '.' /* special variables */;
}

int
is_recipe_at_char(Lexer *lexer, char c) {
    if (lexer->recipe_prefix_char) {
        return c == lexer->recipe_prefix_char;
    }
    return c == '\t';
}

void
consume_word(Lexer *lexer) {
    lstr p = lexer->input;
    while (1) {
        char c = p[lexer->pos];
        int isword = ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
            ('0' <= c && c <= '9') ||
            '_' == c || '.' == c || '/' == c || '-' == c;
        if (!isword)
            break;
        lexer->pos++;
    }
}

void
print_context_at(Lexer *lexer, int pos, char const* optional_prefix) {
    // let's print the whole line.
    int line_start_pos = pos;
    while (line_start_pos != 0 && lexer->input[line_start_pos-1] != '\n') {
        line_start_pos--;
    }
    int line_end_pos = pos;
    while (line_end_pos != lexer->endpos && lexer->input[line_end_pos] != '\n') {
        line_end_pos++;
    }
    printf("\n");
    if (optional_prefix)
        printf("%s: ", optional_prefix);
    printf("%.*s\n", line_end_pos - line_start_pos, &lexer->input[line_start_pos]);
    if (optional_prefix)
        printf("%s: ", optional_prefix);
    printf("%*s^", pos - line_start_pos, "");
}

void
print_error_at(Lexer *lexer, int pos) {
    print_context_at(lexer, pos, "error");
}

void
lexer_rewind(Lexer *lexer, int pos) {
    assert(0 <= pos && pos <= lexer->endpos);
    lexer->pos = pos;
}

int lexer_expect_char_at_pos(Lexer *lexer, char* context, char expected_char, char const* optional_name_of_expected_char, int pos) {
    char c = lexer->input[pos];
    int success = c == expected_char;
    if (!success) {
        print_error_at(lexer, pos);

        printf("error during lexing of %s at byte %d, expected ", context, pos);
        if (optional_name_of_expected_char) {
            printf("%s", optional_name_of_expected_char);
        } else {
            printf("'%c'", expected_char);
        }
        printf(" got '%c' (%d)\n", c, c);
    }
    return success;
}

int
lexer_expect_char(Lexer *lexer, char* context, char expected_char, char const* optional_name_of_expected_char) {
    if (lexer_expect_char_at_pos(lexer, context, expected_char, optional_name_of_expected_char, lexer->pos)) {
        lexer->pos++;
        return 1;
    }
    return 0;
}

int
expect_eol(Lexer *lexer) {
    if (lexer_expect_char(lexer, "eol", '\n', "end-of-line")) {
        return 1;
    }
    return 0;
}

int
matches_any_eol(Lexer *lexer, int *len_of_eol) {
    char c = lexer->input[lexer->pos];
    switch (c) {
        case '\r': {
            if (lexer->pos >= lexer->endpos) {
                print_error_at(lexer, lexer->pos);
                printf("expected \n, got end of file.\n");
                *len_of_eol = 1;
                return 1;
            }
            lexer_expect_char_at_pos(lexer, "windows-style end of line", '\n', "LF: line feed / \\n", lexer->pos + 1);
            *len_of_eol = 2;
            return 1;
        } break;
        case '\n': *len_of_eol = 1;  return 1;
    }
    return 0;
}

void
consume_line(Lexer *lexer) {
    while (1) {
        if (lexer->input[lexer->pos] == '\\') {
            lexer->pos++;
        } else if (lexer->input[lexer->pos] == '\r') {
            if (lexer->input[lexer->pos + 1] != '\n') {
                print_error_at(lexer, lexer->pos);
                printf("expected \\n, got %c.\n", lexer->input[lexer->pos + 1]);
            }
            break;
        } else if (lexer->input[lexer->pos] == '\n') {
            break;
        }
        lexer->pos++;
    }
}

void
terminate_token(Lexer *lexer, Token *tok) {
    tok->len = lexer->pos - tok->pos;
}


Token
next_token_internal(Lexer *lexer) {
    lstr p = lexer->input;
    while (1) {
        char c = p[lexer->pos];
        switch (c) {
            case '.': {
                Token special_variable = { .pos = lexer->pos };
                consume_word(lexer);
                terminate_token(lexer, &special_variable);
                return special_variable;
            }
            case '\\': {
                lexer->pos++;
                if (lexer->input[lexer->pos] == '\r') {
                    lexer->pos++;
                }
                Token escaped_char = { .pos = lexer->pos, .kind = TokenKind_Escape };
                expect_eol(lexer);
                terminate_token(lexer, &escaped_char);
                return escaped_char;
            }
            case '#': consume_line(lexer); break;
            case ' ': {
                Token space = { .pos = lexer->pos };
                consume_whitespace(lexer);
                terminate_token(lexer, &space);
                return space;
            }
            case '\r': {
                lexer->pos++;
                if (expect_eol(lexer)) {
                    Token newline = { .pos = lexer->pos, .kind = TokenKind_Eol };
                    lexer->logical_line++;
                    terminate_token(lexer, &newline);
                    return newline;
                }
            } break;
            case '\n': {
                Token newline = { .pos = lexer->pos, .kind = TokenKind_Eol };
                lexer->pos++;
                lexer->logical_line++;
                terminate_token(lexer, &newline);
                return newline;
            } break;
            case 0: return eof_token(lexer);
            case ':': {
                // either rule or assignment
                if (lexer->input[lexer->pos + 1] == '=') {
                    Token assign_token = { .pos = lexer->pos, };
                    lexer->pos += 2;
                    assign_token.kind = TokenKind_Assignment;
                    terminate_token(lexer, &assign_token);
                    return assign_token;
                }
                Token char_token = { .pos = lexer->pos, .len = 1 };
                lexer->pos++;
                return char_token;
            }
            case '?': case '!': case '+': {
                if (lexer->input[lexer->pos + 1] == '=') {
                    Token assign_token = { .pos = lexer->pos++ };
                    if (lexer_expect_char(lexer, "assignment", '=', 0)) {
                        assign_token.kind = TokenKind_Assignment;
                        terminate_token(lexer, &assign_token);
                        return assign_token;
                    }
                }
                Token char_token = { .pos = lexer->pos, .len = 1 };
                lexer->pos++;
                return char_token;
            } break;
            case '$': case '(': case ')': {
                // these tokens stand for themselves.
                Token char_token = { .pos = lexer->pos++, .len = 1 };
                return char_token;
            } break;
            case '=': {
                Token char_token = { .pos = lexer->pos++, .len = 1 };
                char_token.kind = TokenKind_Assignment;
                return char_token;
            } break;
            default:
                if (is_recipe_at_char(lexer, c)) {
                    if (lexer->pos == 0 || lexer->input[lexer->pos - 1] == '\n') {
                        Token recipe_token = { .pos = lexer->pos, };
                        consume_line(lexer);
                        recipe_token.kind = TokenKind_Recipe;
                        terminate_token(lexer, &recipe_token);
                        return recipe_token;
                    } else {
                        Token char_token = { .pos = lexer->pos++, .len = 1 };
                        return char_token;
                    }
                } else if (is_word_at_char(c)) {
                    Token word_token = { .pos = lexer->pos++ };
                    consume_word(lexer);
                    terminate_token(lexer, &word_token);
                    return word_token;
                }

                Token unknown_token = { .pos = lexer->pos++, .len = 1 };
                return unknown_token;
        }
    }

    return eof_token(lexer);
}

Token next_token(Lexer *lexer) {
    Token tok = next_token_internal(lexer);
    lexer->num_tokens++;
    return tok;
}

void
build_create(Build *build) {
    arena_create(&build->arena, 2*1024*1024);
    anew(build->modules_header, build->modules);
}

void
build_free(Build *build) {
    arena_free(&build->arena);
    afree(build->modules_header, build->modules);
    free(build->module_name_hashes);
}

void
build_add_module(Build *build, Interpreter *interpreter, lstr module_name) {
    for (int n = strlen(module_name); n != 0;) {
        uint64_t hashvalue = hash(module_name, n);
        for (size_t i = 0; i < build->modules_header.size; i++) {
            if (build->module_name_hashes[i] == hashvalue) {
                if (0 == strncmp(build->modules[i].name, module_name, n)) {
                    printf("MMM: warning: trying to add module '%s' that's already been added! while interpreting %s\n", module_name, interpreter->filename);
                    return;
                }
            }
        }

        char *p = arena_alloc(n + 1, &build->arena);
        if (!p) {
            printf("error: exhausted module names memory.\n");
            assert(0);
            exit(0);
        }
        memcpy(p, module_name, n + 1);

        if (buf_wouldgrow(&build->modules_header, 1)) {
            BufHeader *hdr = &build->modules_header;
            int new_capacity = buf_fit_capacity(*hdr, 1);
            build->modules = recallocz(build->modules, hdr->capacity, new_capacity, sizeof build->modules[0]);
            build->module_name_hashes = recallocz(build->module_name_hashes, hdr->capacity, new_capacity, sizeof build->module_name_hashes[0]);
            hdr->capacity = new_capacity;
        }
        build->modules[build->modules_header.size] = (Module) { .name = p };
        build->module_name_hashes[build->modules_header.size] = hashvalue;
        build->modules_header.size++;

        break;
    }
}

int
chars_matches_keyword(char const *keyword, Charbuf const chars) {
    // @slow
    int n = strlen(keyword);
    if (chars.header.size != n) return 0;
    return 0 == strncmp(chars.data, keyword, n);
}

int
token_matches_keyword(char const *keyword, Token tok, Lexer *lexer) {
    // @slow
    int n = strlen(keyword);
    if (tok.len != n) return 0;
    return 0 == strncmp(&lexer->input[tok.pos], keyword, n);
}

int
matches_eol(Token tok) {
    return tok.kind == TokenKind_Eol;
}

int
matches_word(Token tok) {
    return tok.kind == TokenKind_None;
}

int
matches_char(Token tok, char c, Lexer *lexer) {
    return tok.len == 1 && lexer->input[tok.pos] == c;
}

lstr text(Token tok, Lexer *lexer) {
    return &lexer->input[tok.pos];
}

int lookup_variable_index(Interpreter *self, lstr key) {
    // O(n2) for now
    int key_n = strlen(key);
    for (int i = 0; i < self->variables.n; i++) {
        if (self->variables.names_len[i] != key_n)
            continue;
        if (strncmp(self->variables.names[i], key, key_n))
            continue;
        return i;
    }
    return self->variables.n;
}

typedef struct VariableLookup {
  lstr value;
  int is_recursive;
} VariableLookup;

VariableLookup lookup_variable(Interpreter *self, lstr key) {
    if (self->variables.n == 0) return (VariableLookup){ .value = 0 };
    int i = lookup_variable_index(self, key);
    if (i == self->variables.n) return (VariableLookup) { .value = 0 };
    return (VariableLookup){ .value = self->variables.values[i], .is_recursive = self->variables.is_recursive[i] };
}

void set_variable(Interpreter *self, lstr key, lstr value, int is_recursive) {
    int i = lookup_variable_index(self, key);
    assert(0 <= i && i <= self->variables.n);
    if (i == self->variables.n && i >= self->variables.cap) {
        int old_cap = self->variables.cap;
        int new_cap = align_up(old_cap + old_cap + 1, 16);
        self->variables.names = recallocz(self->variables.names, old_cap, new_cap, sizeof self->variables.names[0]);
        self->variables.names_len = recallocz(self->variables.names_len, old_cap, new_cap, sizeof self->variables.names_len[0]);
        self->variables.values = recallocz(self->variables.values, old_cap, new_cap, sizeof self->variables.values[0]);
        self->variables.is_recursive = recallocz(self->variables.is_recursive, old_cap, new_cap, sizeof self->variables.is_recursive[0]);
        self->variables.cap = new_cap;
    }
    if (i == self->variables.n) { // new name
        self->variables.n++;
        int n = strlen(key);
        self->variables.names_len[i] = n;
        self->variables.names[i] = strdup(key);
        self->variables.is_recursive[i] = is_recursive;
    }
    char *old_value = self->variables.values[i];
    self->variables.values[i] = strdup(value);
    free(old_value);
}

void
interpreter_error(Interpreter *interpreter, char* context, Token tok) {
    Lexer *lexer = interpreter->lexer;
    print_error_at(lexer, tok.pos);

    printf("error: while %s at byte %d,", context, lexer->pos);
    printf(" got '%.*s'\n", tok.len, text(tok, lexer));
}

int
matches_space(Token tok, Lexer *lexer) {
    return matches_char(tok, ' ', lexer);
}

int
expects_space(Interpreter *interpreter) {
    Lexer *lexer = interpreter->lexer;
    Token tok = next_token(lexer);
    if (!matches_space(tok, lexer)) {
        interpreter_error(interpreter, "expecting space", tok);
        return 0;
    }
    return 1;
}

void
interpreter_free(Interpreter *self) {
    chars_free(&self->tmpbuf);
    for (int i = 0; i < self->variables.n; i++) {
        free(self->variables.names[i]);
        free(self->variables.values[i]);
    }
    free(self->variables.names);
    free(self->variables.names_len);
    free(self->variables.values);
    memset(&self->variables, 0, sizeof self->variables);
}

int
interpret_function_argument(Interpreter *self, Charbuf *result) {
    Lexer *lexer = self->lexer;
    Token tok = { 0 };
    while (lexer->pos < lexer->endpos) {
        int old_pos = lexer->pos;
        tok = next_token(lexer);
        if (matches_char(tok, ',', lexer)) {
            break;
        } else if (matches_char(tok, ')', lexer)) {
            lexer_rewind(lexer, old_pos);
            break;
        } else {
            chars_push_nstr(result, tok.len, text(tok, lexer));
        }
    }
    return 1;
}

// either variable or function
int
interpret_reference(Interpreter *interpreter, Charbuf *result) {
    Lexer *lexer = interpreter->lexer;
    Token tok;
    tok = next_token(lexer);
    if (matches_char(tok, '$', lexer)) {
        chars_push_nstr(result, 1, "$");
        return 1;
    }
    if (!matches_char(tok, '(', lexer)) {
        interpreter_error(interpreter, "expecting ( at start of function or variable reference", tok);
        return 0;
    }
    Charbuf variable_name = { 0 };
    while (lexer->pos < lexer->endpos) {
        tok = next_token(lexer);
        if (matches_char(tok, ')', lexer) ||
            matches_char(tok, ' ', lexer)) {
            break;
        } else if (matches_char(tok, '$', lexer)) { // references can be nested
            Charbuf subreference_result = { 0 };
            int subreference = interpret_reference(interpreter, &subreference_result);
            if (!subreference)
                return 0;
            chars_push_nstr(&variable_name, subreference_result.header.size, subreference_result.data);
            chars_free(&subreference_result);
        } else {
            chars_push_nstr(&variable_name, tok.len, text(tok, lexer));
        }
    }

    if (matches_char(tok, ' ', lexer)) {
        // a function starts with a name, then a delimiter then its arguments.

        // builtins:
        if (chars_matches_keyword("error", variable_name) ||
            chars_matches_keyword("info", variable_name) ||
            chars_matches_keyword("warning", variable_name)) {
            // no-op
            if (emit_debug_log) {
                print_context_at(lexer, tok.pos, "FFF");
                printf("$(%.*s...) control function\n", variable_name.header.size, variable_name.data);
            }
        }
        else if (chars_matches_keyword("addprefix", variable_name) ||
                 chars_matches_keyword("addsuffix", variable_name)) {
            printf("$(%.*s...) text function\n", variable_name.header.size, variable_name.data);
        }
        else if (chars_matches_keyword("patsubst", variable_name)) {
            printf("$(%.*s...) substitution function\n", variable_name.header.size, variable_name.data);
        }
        // user-defined
        else if (chars_matches_keyword("call", variable_name)) {
            int function_pos = tok.pos;
            Charbuf user_function_name = { 0 };
            // @todo this appears to fail with to-lib-$(ARCH):
            interpret_function_argument(interpreter, &user_function_name);


            print_context_at(lexer, function_pos, "FFF");
            printf("call to user defined function '%s'\n", user_function_name.data);

            chars_free(&user_function_name);
        } else {
            // not a known function...
            print_context_at(lexer, tok.pos, "FFF");
            printf("Unknown function '%.*s'\n", variable_name.header.size, variable_name.data);
        }

        // consume arguments to function:
        Charbuf arguments = { 0 };
        while (lexer->pos < lexer->endpos) {
            tok = next_token(lexer);
            if (matches_char(tok, ')', lexer)) {
                break;
            } else if (matches_char(tok, '$', lexer)) { // references can be nested
                Charbuf subreference_result = { 0 };
                int subreference = interpret_reference(interpreter, &subreference_result);
                if (!subreference)
                    return 0;
                chars_push_nstr(&arguments, subreference_result.header.size, subreference_result.data);
                chars_free(&subreference_result);
            } else {
                chars_push_nstr(&arguments, tok.len, text(tok, lexer));
            }
        }
        return 1;
    }

    if (!matches_char(tok, ')', lexer)) {
        interpreter_error(interpreter, "expected ) at end of function or variable reference", tok);
        return 0;
    }

    VariableLookup var = lookup_variable(interpreter, variable_name.data);
    if (!var.value) {
        printf("error: could not find value of variable '%s'\n", variable_name.data);
        return 0;
    }
    chars_push_nstr(result, strlen(var.value), var.value);
    return 1;
}

int
interpret_filename(Interpreter *interpreter, Charbuf *result) {
    // I don't think there's anything specially filenamy about this, it's a generic interpretation of a string.

    Lexer *lexer = interpreter->lexer;
    while (lexer->pos < lexer->endpos) {
        int old_pos = lexer->pos;
        Token tok = next_token(lexer);
        if (matches_eol(tok) || matches_space(tok, lexer)) {
            lexer_rewind(lexer, old_pos);
            break;
        }
        if (matches_char(tok, '$', lexer)) {
            Charbuf reference_value = { 0 };
            if (!interpret_reference(interpreter, &reference_value)) {
                printf("\nVariable/function reference: '%.*s' evaluation failed\n", lexer->pos - tok.pos, &lexer->input[tok.pos]);
                interpreter_error(interpreter, "evaluating reference", tok);
                return 0;
            }
            chars_push_nstr(result, reference_value.header.size, reference_value.data);
            chars_free(&reference_value);
        } else {
            chars_push_nstr(result, tok.len, text(tok, lexer));
        }
    }
    return 1;
}

void interpreter_load_file(Interpreter *interpreter, char *filename, int is_optional);

void
interpret_find_and_load_file(Interpreter *interpreter, char *filename_spec, int is_optional) {
    // @todo implement lookup in various include-dirs.
    interpreter_load_file(interpreter, filename_spec, is_optional);
}

void
interpret_include(Interpreter *interpreter, int is_optional) {
    if (emit_debug_log) {
        printf("III: include directive here in this line: ");
        print_context_at(interpreter->lexer, interpreter->lexer->pos, 0);
    }

    expects_space(interpreter);
    buf_reset(&interpreter->tmpbuf.header);
    if (!interpret_filename(interpreter, &interpreter->tmpbuf)) {
        return;
    }
    interpret_find_and_load_file(interpreter, interpreter->tmpbuf.data, is_optional);

    Lexer *lexer = interpreter->lexer;
    while (lexer->pos < lexer->endpos) {
        Token tok = next_token(interpreter->lexer);
        if (matches_eol(tok)) {
            break;
        }
        if (!matches_space(tok, lexer)) {
            interpreter_error(interpreter, "expected space between the filenames of an include directive", tok);
            break;
        }
        buf_reset(&interpreter->tmpbuf.header);
        interpret_filename(interpreter, &interpreter->tmpbuf);
        interpret_find_and_load_file(interpreter, interpreter->tmpbuf.data, is_optional);
    }
}

int
interpret_word(Interpreter *self, Token tok, Charbuf *result) {
    Lexer *lexer = self->lexer;
    if (matches_char(tok, '$', lexer)) {
        Charbuf reference_value = { 0 };
        if (!interpret_reference(self, &reference_value)) {
            printf("\nVariable/function reference: '%.*s' evaluation failed\n", lexer->pos - tok.pos, &lexer->input[tok.pos]);
            interpreter_error(self, "evaluating reference", tok);
            return 0;
        }
        chars_push_nstr(result, reference_value.header.size, reference_value.data);
        chars_free(&reference_value);
    } else {
        chars_push_nstr(result, tok.len, text(tok, lexer));
    }
    return 1;
}

int
interpret_assignment(Interpreter *self, Token first_token) {
    Lexer *lexer = self->lexer;
    Token tok;

    do {
        tok = next_token(lexer);
    } while (matches_space(tok, lexer));

    if (tok.kind == TokenKind_Assignment) {
        if (emit_debug_log) {
            printf("AAA: assignment found.");
            print_context_at(lexer, tok.pos, "AAA");
            printf(" variable name is '%.*s'", first_token.len, text(first_token, lexer));
        }

        int all_caps = 1;
        for (int i = 0; all_caps && i < first_token.len; i++) {
            char c = lexer->input[first_token.pos + i];
            if ('a' <= c && c <= 'z')
                all_caps = 0;
        }
        if (emit_debug_log)
            if (all_caps)
                printf(" (parameter for implicit rules or user-overridable parameter)");
        int c = text(tok, lexer)[0];

        if (emit_debug_log)
            printf(" flavor:");

        int is_recursive = c == ':' ? 0 : 1;
        switch (c) {
            break; case ':': if (emit_debug_log) printf(" simply expanded\n");
            break; case '=': if (emit_debug_log) printf(" recursively expanded\n");
            break; case '?': if (emit_debug_log) printf(" conditional, recursively expanded\n");
            break; case '+': if (emit_debug_log) printf(" appending (flavor unchanged)\n");
            break; default: {
                print_error_at(lexer, first_token.pos);
                printf("  unknown type (%c)\n", c);
                return 0;
            }
        }

        int success;
        // consume value.
        consume_whitespace(lexer);
        Charbuf value = { 0 };
        if (!is_recursive) {
            // expand simply expanded variables
            while (lexer->pos < lexer->endpos) {
                tok = next_token(lexer);
                if (matches_eol(tok))
                    break;
                if (!interpret_word(self, tok, &value)) {
                    success = 0;
                    chars_free(&value);
                    value = (Charbuf){ 0 };
                    break;
                }
            }
        } else {
            while (lexer->pos < lexer->endpos) {
                tok = next_token(lexer);
                if (matches_eol(tok)) {
                    break;
                }
                chars_push_nstr(&value, tok.len, text(tok, lexer));
            }
        }
        if (!value.data)
            return 0;

        // @todo @wip set variables, taking into account the type of the variable and the assignment operator.
        Charbuf variable_name = { 0 };
        chars_push_nstr(&variable_name, first_token.len, text(first_token, lexer));
        set_variable(self, variable_name.data, value.data, is_recursive);

        if (chars_matches_keyword("1", value)) {
            // likely a module?
            printf("MMM: are you a module? %s\n", variable_name.data);
            if (self->build) build_add_module(self->build, self, variable_name.data);
        }


        chars_free(&variable_name);
        chars_free(&value);

        return 1;
    }
    return 0;
}

int
interpret_rule_target(Interpreter *interpreter, Charbuf *result) {
    Lexer *lexer = interpreter->lexer;
    while (lexer->pos < lexer->endpos) {
        int old_pos = lexer->pos;
        Token tok = next_token(lexer);
        if (matches_eol(tok) || matches_space(tok, lexer) || matches_char(tok, ':', lexer)) {
            lexer_rewind(lexer, old_pos);
            break;
        }
        if (!interpret_word(interpreter, tok, result)) {
            return 0;
        }
    }
    return 1;
}

int
interpret_rule(Interpreter *self, Token first_token) {
    Lexer *lexer = self->lexer;

    Charbuf target_buf = { 0 };
    lexer_rewind(lexer, first_token.pos);
    if (!interpret_rule_target(self, &target_buf))
        goto not_a_rule;
    printf("rule target: %s\n", target_buf.data);

    Token tok = { 0 };
    while (lexer->pos < lexer->endpos) {
        int old_pos = lexer->pos;
        tok = next_token(lexer);
        if (!matches_char(tok, ' ', lexer)) {
            lexer_rewind(lexer, old_pos);
            break;
        }
        buf_reset(&target_buf.header);
        if (!interpret_rule_target(self, &target_buf))
            goto not_a_rule;

        printf("rule target: %s\n", target_buf.data);
    }

    if (!matches_char(tok, ':', lexer)) {
        goto not_a_rule;
    }

    while (lexer->pos < lexer->endpos) {
        tok = next_token(lexer);
        if (matches_eol(tok))
            break;
    }

    while (lexer->pos < lexer->endpos) {
        int old_pos = lexer->pos;
        tok = next_token(lexer);
        if (tok.kind != TokenKind_Recipe) {
            lexer_rewind(lexer, old_pos);
            break;
        }
        expect_eol(lexer);
    }

    lstr target_name = target_buf.data;
    if (emit_debug_log) {
        printf("RRR: found rule");
        print_context_at(lexer, first_token.pos, "RRR");
        printf("rule here has target name '%s'\n", target_name);
    }

    chars_free(&target_buf);
    return 1;
not_a_rule:
    print_context_at(lexer, first_token.pos, "XXX");
    printf("XXX: not a rule at pos %d", first_token.pos);
    print_context_at(lexer, first_token.pos, "XXX");
    chars_free(&target_buf);
    return 0;
}

int
interpret_conditional(Interpreter* interpreter, Token tok) {
    Lexer *lexer = interpreter->lexer;

    if (token_matches_keyword("ifeq", tok, lexer)) {
        return 1;
    } else if (token_matches_keyword("ifneq", tok, lexer)) {
        return 1;
    } else if (token_matches_keyword("else", tok, lexer)) {
        return 1;
    } else if (token_matches_keyword("endif", tok, lexer)) {
        return 1;
    } else if (token_matches_keyword("ifndef", tok, lexer)) {
        // @todo implement me
        // evaluate the right-hand expression, lookup the existence of the variable, and if it
        // exists, ignore all the lines between here and the else/endif at the same scoping level.
        return 1;
    }
    return 0;
}

int
interpret_toplevel(Interpreter* interpreter) {
    Lexer *lexer = interpreter->lexer;

    char const* sep = "\n";
    Token tok = { 0 };
    while (lexer->pos < lexer->endpos) {
        consume_whitespace(lexer);
        tok = next_token(lexer); // first token in the line.

        // parse directives:
        if (token_matches_keyword("include", tok, lexer) ||
            token_matches_keyword("-include", tok, lexer) ||
            token_matches_keyword("sinclude", tok, lexer)) {
            int is_optional = text(tok, lexer)[0] != 'i';
            interpret_include(interpreter, is_optional);
            return 1;
        } else if (interpret_conditional(interpreter, tok)) {
            goto error_recovery;
        } else if (token_matches_keyword("define", tok, lexer)) {
            // @todo implement me.
            goto error_recovery;
        } else if (token_matches_keyword("endef", tok, lexer)) {
            // @todo implement me.
            goto error_recovery;
        } else if (matches_word(tok)) {
            if (interpret_assignment(interpreter, tok)) {
                return 1;
            } else if (interpret_rule(interpreter, tok)) {
                return 1;
            } else {
                goto error_recovery;
            }
        } else if (matches_eol(tok)) {
            sep = "\n";
            continue;
        }

        printf("%s", sep);
        printf("([%.*s]@%d)", tok.len, text(tok, lexer), tok.pos);
        sep = ", ";
    }

    return 0;

error_recovery:
    printf("error:");
    print_context_at(lexer, lexer->pos, "error");
    printf("unknown (skipping whole line)\n");
    while (lexer->pos < lexer->endpos) {
        tok = next_token(lexer);
        if (matches_eol(tok)) {
            return 1;
        }
    }
    return 1;
}

void interpreter_load_file(Interpreter *interpreter, char *filename, int is_optional) {
    Lexer *old_lexer = interpreter->lexer;

    char *old_filename = interpreter->filename;

    size_t num_bytes = 0;
    char* err = 0;
    char *file_content = read_whole_file(filename, &num_bytes, &err);
    if (err) {
        if (is_optional) return; // silent
        printf("error: while reading file %s: %s\n", filename, err);
        return;
    }

    interpreter->filename = filename;
    Lexer lexer = { .input = file_content, .endpos = num_bytes, 0 };
    interpreter->lexer = &lexer;

    while (interpret_toplevel(interpreter)) {
        // continue;
    }

    printf("Stats for %s:\n", filename);
    printf("num_bytes: %ld\n", (long unsigned)num_bytes);
    printf("num_tokens: %d\n", lexer.num_tokens);
    printf("avg_byte_per_token: %f\n", 1.0 * num_bytes / lexer.num_tokens);
    free(file_content);

    interpreter->filename = old_filename;
    interpreter->lexer = old_lexer;
}

void
process_ysr_file(Project *project, char *filename) {
    Build build = { 0, };
    Interpreter interpreter = { 0, };
    set_variable(&interpreter, "TOP", project->topdir, 0);
    set_variable(&interpreter, "YSR.project.file", project->projectfile, 0);
    set_variable(&interpreter, "YSR.libdir", project->ysrlibdir, 0);
    set_variable(&interpreter, "HOST_CONFIG_MK", project->host_config_mk, 0);

    set_variable(&interpreter, "DEST", "<dest>", 1);

    build_create(&build);

    interpreter.build = &build;

    interpreter_load_file(&interpreter, filename, 0);

    build_free(&build);
    interpreter_free(&interpreter);
}

int
main(void) {
    Project project = {
        .topdir = "h:/ln2/trunk",
        .projectfile = "h:/ln2/trunk/project.ysr",
        .ysrlibdir = "h:/ysr/lib",
        .host_config_mk = "h:/ln2/trunk/ysr/local-config.mk",
    };
    char *filenames_data[] = {
        "h:/ln2/trunk/plugins/Gordia/Makefile",
    };
    size_t num_filenames = sizeof filenames_data / sizeof filenames_data[0];

    for (size_t i = 0; i < num_filenames; i++) {
        printf("----\nhello %s\n", filenames_data[i]);
        process_ysr_file(&project, filenames_data[i]);
    }

    return 0;
}
