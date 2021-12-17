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

typedef char const *lstr;

typedef struct Charbuf {
    int size;
    int capacity;
    char *data;
} Charbuf;

typedef struct ArrayHeader {
    size_t count;
} ArrayHeader;

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
    TokenKind_Eol,
    TokenKind_Escape,
} TokenKind;

typedef struct Token {
    int pos, len;
    char kind;
} Token;

// You can't really parse and lex makefiles without also interpreting them, since variables definitions have direct influences on
typedef struct Interpreter {
    Lexer *lexer;
    Charbuf tmpbuf;

    struct {
        int n;
        int cap;
        lstr *names;
        int *names_len;
        lstr *values;
    } variables;
} Interpreter;


int align_up(int size, int alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

int greater_of(int a, int b) {
    return a > b ? a : b;
}

void* reallocz(void *ptr, size_t old_size, size_t size) {
    ptr = realloc(ptr, size);
    if (size > old_size) {
        char *bytes = ptr;
        memset(&bytes[old_size], 0, size - old_size);
    }
    return ptr;
}

void* recallocz(void *ptr, size_t old_num, size_t new_num, size_t elem_size) {
    return reallocz(ptr, old_num * elem_size, new_num * elem_size);
}

void
chars_reset(Charbuf *buf) {
    buf->size = 0;
}

void
chars_free(Charbuf *buf) {
    chars_reset(buf);
    free(buf->data);
    buf->data = 0;
    buf->capacity = 0;
}

// returns 1 if it needs a realloc for n additional bytes
int
chars_wouldgrow(Charbuf *buf, int n) {
    return buf->capacity - buf->size < n;
}

void
chars_push_nstr(Charbuf *buf, size_t n, lstr str) {
    if (chars_wouldgrow(buf, n)) {
        int new_capacity = align_up(greater_of(2*buf->capacity, buf->size + n + 1 /* implicit zero terminator */), 4096);
        buf->data = reallocz(buf->data, buf->capacity, new_capacity);
        buf->capacity = new_capacity;
    }
    memcpy(&buf->data[buf->size], &str[0], n);
    buf->data[buf->size + n] = '\0'; // always null terminate the strings for compat with C
    buf->size += n;
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
    return (struct Token) { lexer->endpos };
}

void
consume_whitespace(Lexer *lexer) {
    lstr p = lexer->input;
    while (p[lexer->pos] == ' ') {
        lexer->pos++;
    }
}

int
is_word_at_char(char c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || '.' /* special variables */;
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

int
expect_eol(Lexer *lexer) {
    if (lexer_expect_char(lexer, "eol", '\n', "end-of-line")) {
        return 1;
    }
    return 0;
}

void
consume_line(Lexer *lexer) {
    while (1) {
        int len;
        if (matches_any_eol(lexer, &len)) {
            (void) len;
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
            default:
                if (is_recipe_at_char(lexer, c)) {
                    if (lexer->pos == 0 || lexer->input[lexer->pos - 1] == '\n') {
                        Token recipe_token = { .pos = lexer->pos, };
                        consume_line(lexer);
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

int
matches_keyword(char const* keyword, Token tok, Lexer *lexer) {
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

lstr lookup_variable(Interpreter *self, lstr key) {
    if (self->variables.n == 0) return 0;
    return self->variables.values[lookup_variable_index(self, key)];
}

void set_variable(Interpreter *self, lstr key, lstr value) {
    int i = lookup_variable_index(self, key);
    assert(0 <= i && i <= self->variables.n);
    if (i == self->variables.n && i >= self->variables.cap) {
        int old_cap = self->variables.cap;
        int new_cap = align_up(old_cap + old_cap + 1, 16);
        self->variables.names = recallocz(self->variables.names, old_cap, new_cap, sizeof self->variables.names[0]);
        self->variables.names_len = recallocz(self->variables.names_len, old_cap, new_cap, sizeof self->variables.names_len[0]);
        self->variables.values = recallocz(self->variables.values, old_cap, new_cap, sizeof self->variables.values[0]);
        self->variables.cap = new_cap;
    }
    if (i == self->variables.n) { // new name
        self->variables.n++;
        int n = strlen(key);
        self->variables.names_len[i] = n;
        self->variables.names[i] = strdup(key);
    }
    lstr old_value = self->variables.values[i];
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

// either variable or function
int
interpret_reference(Interpreter *interpreter, Charbuf *result) {
    Lexer *lexer = interpreter->lexer;
    Token tok;
    tok = next_token(lexer);
    if (!matches_char(tok, '(', lexer)) {
        interpreter_error(interpreter, "expecting ( at start of function or variable reference", tok);
        return 0;
    }
    Charbuf variable_name = { 0 };
    while (lexer->pos < lexer->endpos) {
        tok = next_token(lexer);
        if (matches_char(tok, ')', lexer)) {
            break;
        } else if (matches_char(tok, '$', lexer)) { // references can be nested
            Charbuf subreference_result = { 0 };
            int subreference = interpret_reference(interpreter, &subreference_result);
            if (!subreference)
                return 0;
            chars_free(&subreference_result);
            assert(0); return 0; // not implemented yet.
        } else {
            chars_push_nstr(&variable_name, tok.len, text(tok, lexer));
        }
    }
    if (!matches_char(tok, ')', lexer)) {
        interpreter_error(interpreter, "expected ) at end of function or variable reference", tok);
        return 0;
    }
    lstr value = lookup_variable(interpreter, variable_name.data);
    if (!value) {
        return 0;
    }
    chars_push_nstr(result, strlen(value), value);
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
            chars_push_nstr(result, reference_value.size, reference_value.data);
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
    printf("debug: attempting to load file: %s\n", filename_spec);
    interpreter_load_file(interpreter, filename_spec, is_optional);
}

void
interpret_include(Interpreter *interpreter, int is_optional) {
    printf("III: include directive here in this line: ");
    print_context_at(interpreter->lexer, interpreter->lexer->pos, 0);

    expects_space(interpreter);
    chars_reset(&interpreter->tmpbuf);
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
        chars_reset(&interpreter->tmpbuf);
        interpret_filename(interpreter, &interpreter->tmpbuf);
        interpret_find_and_load_file(interpreter, interpreter->tmpbuf.data, is_optional);
    }
}

int
interpret_toplevel(Interpreter* interpreter) {
    Lexer *lexer = interpreter->lexer;

    char const* sep = "\n";
    while (lexer->pos < lexer->endpos) {
        Token tok = next_token(lexer); // first token in the line.

        printf("%s", sep);

        // parse directives:
        if (matches_keyword("include", tok, lexer) ||
            matches_keyword("-include", tok, lexer) ||
            matches_keyword("sinclude", tok, lexer)) {
            int is_optional = text(tok, lexer)[0] != 'i';
            interpret_include(interpreter, is_optional);
            return 1;
        } else if (matches_eol(tok)) {
            sep = "\n";
        } else {
            printf("([%.*s]@%d)", tok.len, text(tok, lexer), tok.pos);
            sep = ", ";
        }

        if (tok.pos >= lexer->endpos) { return 0; }
    }
    return 0;
}

void interpreter_load_file(Interpreter *interpreter, char *filename, int is_optional) {
    Lexer *old_lexer = interpreter->lexer;

    size_t num_bytes = 0;
    char* err = 0;
    char *file_content = read_whole_file(filename, &num_bytes, &err);
    if (err) {
        if (is_optional) return; // silent
        printf("error: while reading file %s: %s\n", filename, err);
        return;
    }
    
    Lexer lexer = { .input = file_content, .endpos = num_bytes, 0 };
    interpreter->lexer = &lexer;
    
    while (interpret_toplevel(interpreter)) {
        // continue;
    }

    printf("Stats for %s:\n", filename);
    printf("num_bytes: %d\n", num_bytes);
    printf("num_tokens: %d\n", lexer.num_tokens);
    printf("avg_byte_per_token: %f\n", 1.0 * num_bytes / lexer.num_tokens);
    free(file_content); 
    
    interpreter->lexer = old_lexer;
}

void
process_ysr_file(Project *project, char *filename) {
    Interpreter interpreter = { 0 };
    set_variable(&interpreter, "TOP", project->topdir);
    set_variable(&interpreter, "YSR.project.file", project->projectfile);
    set_variable(&interpreter, "YSR.libdir", project->ysrlibdir);
    set_variable(&interpreter, "HOST_CONFIG_MK", project->host_config_mk);

    interpreter_load_file(&interpreter, filename, 0);

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
    ArrayHeader filenames;
    filenames.count = sizeof filenames_data / sizeof filenames_data[0];

    for (size_t i = 0; i < filenames.count; i++) {
        printf("----\nhello %s\n", filenames_data[i]);
        process_ysr_file(&project, filenames_data[i]);
    }

    return 0;
}
