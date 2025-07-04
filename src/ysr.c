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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "wyhash.h"

typedef struct Program_Options {
    bool emit_debug_log;
} Program_Options;

Program_Options g_program_options = {
    .emit_debug_log = false,
};

inline uint64_t
bits(uint64_t x, int start, int len) {
    uint64_t mask = (1 << len) - 1;
    return (x >> start) & mask;    
}

enum Mk_Function_Flags {
    MFF_IS_MODULE       = 1 << 0,
    MFF_IS_IPLUG        = 1 << 1,
};

typedef struct Mk_Function_Options {
    bool defines_module;
} Mk_Function_Options;

uint64_t
pack_mk_function_flags(Mk_Function_Options x) {
    return (x.defines_module ? MFF_IS_MODULE : 0);
}

Mk_Function_Options
unpack_mk_function_flags(uint64_t x) {
    return (struct Mk_Function_Options) {
        .defines_module = x & MFF_IS_MODULE,
    };
}

typedef struct Mk_Function {
    char const * name;
    char const * src_makefile;
    
    uint64_t flags;
} Mk_Function;

Mk_Function Ysr_Functions[] = {
    { "ysr-add-c-lib-shared", "lib/templates/library.mk", MFF_IS_MODULE, }, // of (name)
    { "ysr-add-c-lib-static", "lib/templates/library.mk", MFF_IS_MODULE, },
    { "ysr-add-c++-prog", "lib/templates/program.mk", MFF_IS_MODULE, },
    { "ysr-add-c-console-prog", "lib/templates/program.mk", MFF_IS_MODULE, }, // of (name)
    { "require-directory" },
    { "objs-to-deps" }, // of (list of object file names)
    { "ln2-info" }, // of (string)

    // Not sure if this is legit:
    { "mk-c-sharedlib-rule" }, // of (name)
};


Mk_Function Ln2_Functions[] = {
    { "mk-iplug-rule", "third-party/wdl.mk", MFF_IS_MODULE | MFF_IS_IPLUG, }, // of (name)
};

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

#define aadd(hdr, data_ptr, data) do { \
    agrow(hdr, data_ptr, hdr.size + 1); \
    data_ptr[hdr.size] = data; \
    hdr.size++; \
} while (0)

#define afor(varname, hdr) for (size_t (varname) = 0; (varname) < (hdr).size; (varname)++)

inline void assert_index(BufHeader hdr, size_t i) {
    assert(0 <= i && i < hdr.size);
}

int smallsize(size_t x) {
  assert(x < INT_MAX);
  if (x >= INT_MAX) { abort(); }
  return (int) x;
}

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
    char const * filename;
    lstr input;
    int endpos;
    bool expects_recipe;

    int pos;
    int logical_line;
    int physical_line;

    char recipe_prefix_char; // by default \t but can be changed with .RECIPEPREFIX

    int toplevel_pos; // where the current toplevel form started

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
    bool is_defined;
} Module;

typedef struct Build {
    FixedSizeArena arena;
    BufHeader modules_header;
    Module *modules;
    uint64_t *module_name_hashes;
} Build;


// @todo reconcile with Module and Build
typedef struct Module2 {
    Charbuf name;
    uint64_t flags; // Mk_Function_Flags
} Module2;


// You can't really parse and lex makefiles without also interpreting them, since variables definitions have direct influences on
typedef struct Interpreter {
    Project *project;

    Lexer *lexer;
    char *filename;
    char *dirname;
    Charbuf tmpbuf;

    // > A variable is a name defined in a makefile to represent a string of text, called the variable’s value. 
    // > (...) (In some other versions of make, variables are called macros.) 
    struct {
        BufHeader header;
        char **names;
        int *names_len;
        char **values;
        char *is_recursive;
    } variables;

    struct {
        BufHeader header;
        Module2 *data;
    } modules;

    Build *build; // output of our interpreter.
} Interpreter;

uint64_t
hash(char const *bytes, size_t n) {
    return wyhash(bytes, n, 0, _wyp);
}

int
align_up(int size, int alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

int
greater_of(int a, int b) {
    return a > b ? a : b;
}

void*
reallocz(void *ptr, size_t old_size, size_t size) {
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

void*
recallocz(void *ptr, size_t old_num, size_t new_num, size_t elem_size) {
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
    buf->data = 0;
}

// returns 1 if it needs a realloc for n additional bytes
int
buf_wouldgrow(BufHeader *buf, size_t n) {
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
    buf->capacity = smallsize(new_capacity);
}

void
chars_push_nstr(Charbuf *chars, size_t n, lstr str) {
    if (n == 0)
        return;
    BufHeader *buf = (BufHeader*)chars;
    int needed_n = smallsize(n + 1 /* implicit zero terminator */);
    if (buf_wouldgrow(buf, needed_n)) {
        int new_capacity = buf_fit_capacity(*buf, needed_n);
        chars_reserve(chars, new_capacity);
    }
    memcpy(&chars->data[buf->size], &str[0], n);
    chars->data[buf->size + n] = '\0'; // always null terminate the strings for compat with C
    buf->size += smallsize(n);
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

typedef enum ErrorCode {
    ErrorCode_None,
    ErrorCode_FileNotFound,
    ErrorCode_FileReadFailed,
} ErrorCode;

typedef struct Error
{
    ErrorCode code;
    char *message;
    int errno_value;
} Error;

void
error_assert_none(Error *error) {
    if (error->code != ErrorCode_None) {
        printf("Unrecoverable: unprocessed error %d:%s (errno: %d) found\n", error->code, error->message, error->errno_value);
        assert(0);
        exit(1);
    }
}

void
error_set(Error *error, ErrorCode code, char *message) {
    error_assert_none(error);
    error->code = code;
    error->message = message;
    error->errno_value = errno;
}

void
error_clear(Error *error) {
    *error = (Error){ 0 };
}

void
error_free(Error *error) {
    error_assert_none(error);
}
   
static char *
read_whole_file(lstr const filename, size_t *num_bytes_ptr, Error *error) {
    char *result = 0;
    char *buffer = 0;
    int errc = 0;

    *num_bytes_ptr = 0;

    FILE *file = fopen(filename, "rb");
    if (!file) {
        error_set(error, ErrorCode_FileNotFound, "could not open file with fopen");
        return 0;
    }

    errc = fseek(file, 0, SEEK_END);
    if (errc) {
        error_set(error, ErrorCode_FileReadFailed, "could not seek to the end of the file with fseek");
        goto return_with_file_open;
    }

    int num_bytes_or_error_if_negative = ftell(file);
    if (num_bytes_or_error_if_negative < 0) {
        error_set(error, ErrorCode_FileReadFailed, "could not get the size of the file with ftell");
        goto return_with_file_open;
    }

    int num_bytes = num_bytes_or_error_if_negative;

    buffer = calloc(num_bytes + 1 /* null terminator */, 1);
    errc = fseek(file, 0, SEEK_SET); // rewind to beginning.
    if (errc) {
        error_set(error, ErrorCode_FileReadFailed, "could not rewind to beginning of file");
        goto return_with_file_open;
    }
    if (fread(buffer, num_bytes, 1, file) < 1) {
        if (feof(file)) {
            error_set(error, ErrorCode_FileReadFailed, "encountered end of file during fread while reading all the bytes");
            goto return_with_file_open;
        } else {
            error_set(error, ErrorCode_FileReadFailed, "could not read entire file with fread");
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

int
is_number_at_char(char c) {
    return ('0' <= c && c <= '9');
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

    printf("\n%s:top:%d\n", lexer->filename, lexer->toplevel_pos);
    if (optional_prefix)
        printf("%s: ", optional_prefix);
    printf("%s:%d: ", lexer->filename, pos);

    printf("%.*s\n", line_end_pos - line_start_pos, &lexer->input[line_start_pos]);
    if (optional_prefix)
        printf("%s: ", optional_prefix);
    printf("%s:%d: ", lexer->filename, pos);

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
            // > Makefiles use a “line-based” syntax in which the newline character is special and marks the end of a statement. GNU make has no limit on the length of a statement line, up to the amount of memory in your computer.
            //
            // > However, it is difficult to read lines which are too long to display without wrapping or scrolling. So, you can format your makefiles for readability by adding newlines into the middle of a statement: you do this by escaping the internal newlines with a backslash (\) character. Where we need to make a distinction we will refer to “physical lines” as a single line ending with a newline (regardless of whether it is escaped) and a “logical line” being a complete statement including all escaped newlines up to the first non-escaped newline.
            //
            // > The way in which backslash/newline combinations are handled depends on whether the statement is a recipe line or a non-recipe line. Handling of backslash/newline in a recipe line is discussed later (see Splitting Recipe Lines). 
            //
            // From: https://www.gnu.org/software/make/manual/make.html#index-splitting-long-lines

            case 0: {
                return eof_token(lexer);
            }

            case '\r': {
                Token newline = { .pos = lexer->pos, .kind = TokenKind_Eol };
                lexer->pos++;
                if (expect_eol(lexer)) {
                    lexer->logical_line++;
                    lexer->physical_line++;
                    terminate_token(lexer, &newline);
                    return newline;
                }
            } break;

            case '\n': {
                Token newline = { .pos = lexer->pos, .kind = TokenKind_Eol };
                lexer->pos++;
                lexer->logical_line++;
                lexer->physical_line++;
                terminate_token(lexer, &newline);
                return newline;
            } break;

            case '\\': {
                // > Outside of recipe lines, backslash/newlines are converted into a single space character. Once that is done, all whitespace around the backslash/newline is condensed into a single space: this includes all whitespace preceding the backslash, all whitespace at the beginning of the line after the backslash/newline, and any consecutive backslash/newline combinations.                

                lexer->pos++;
                if (lexer->input[lexer->pos] == '\r') {
                    lexer->pos++;
                }
                Token escaped_char = { .pos = lexer->pos, .kind = TokenKind_Escape };
                expect_eol(lexer);
                // We use the \n as our escape character, which should be treated like whitespace.

                lexer->physical_line++;

                terminate_token(lexer, &escaped_char);
                return escaped_char;
            }

            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7':
            case '8': case '9': {
                Token number = { .pos = lexer->pos };
                lexer->pos++;
                while (is_number_at_char(lexer->input[lexer->pos])) {
                    lexer->pos++;
                }
                terminate_token(lexer, &number);
                return number;
            }
            case '.': {
                Token special_variable = { .pos = lexer->pos };
                consume_word(lexer);
                terminate_token(lexer, &special_variable);
                return special_variable;
            }
            case '%': {
                // start of a pattern
                Token pattern = { .pos = lexer->pos };
                lexer->pos++;
                consume_word(lexer);
                terminate_token(lexer, &pattern);

                return pattern;
            }
            case '#': consume_line(lexer); break;
            case ' ': {
                Token space = { .pos = lexer->pos };
                consume_whitespace(lexer);
                terminate_token(lexer, &space);
                return space;
            }

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
            case '-': {
                int const pos0 = lexer->pos + 1;
                int pos = pos0;
                char include_str[] = "include";
                while (include_str[pos - pos0] == lexer->input[pos]) {
                    pos++;
                }
                if (include_str[pos - pos0] == '\0') {
                    Token minus_include_token = { .pos = lexer->pos };
                    lexer->pos = pos;
                    terminate_token(lexer, &minus_include_token);
                    return minus_include_token;
                } else {
                    Token minus_token = { .pos = lexer->pos };
                    lexer->pos++;
                    terminate_token(lexer, &minus_token);
                    return minus_token;
                }
            } break;
            case '$': case '(': case ')': case ',': 
            case '@': case '^': case ';': case '/': 
            case '[': case ']': case '_': case '"': 
            case '\'': case '{': case '}': {
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
                    // > One of the few ways in which make does interpret recipes is checking for a backslash just before the newline. As in normal makefile syntax, a single logical recipe line can be split into multiple physical lines in the makefile by placing a backslash before each newline. A sequence of lines like this is considered a single recipe line, and one instance of the shell will be invoked to run it.
                    //
                    // > However, in contrast to how they are treated in other places in a makefile (see Splitting Long Lines), backslash/newline pairs are not removed from the recipe. Both the backslash and the newline characters are preserved and passed to the shell. How the backslash/newline is interpreted depends on your shell. If the first character of the next line after the backslash/newline is the recipe prefix character (a tab by default; see Other Special Variables), then that character (and only that character) is removed. Whitespace is never added to the recipe. 
                    //
                    // From: https://www.gnu.org/software/make/manual/make.html#Splitting-Recipe-Lines

                    if (lexer->expects_recipe) {
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
                print_error_at(lexer, unknown_token.pos);
                printf("TTT: Unknown token %c\n", lexer->input[unknown_token.pos]);
                return unknown_token;
        }
    }

    return eof_token(lexer);
}

// It's kind of a redflag if you have to use this function in the parser...
Token
lexer_token_at(Lexer *lexer, int pos) {
    Lexer temp_lexer = *lexer;
    temp_lexer.pos = pos;
    return next_token_internal(&temp_lexer);
}

Token
next_token(Lexer *lexer) {
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

typedef struct Add_Module_Options {
    bool do_define;
} Add_Module_Options;

void
build_add_module(Build *build, Interpreter *interpreter, lstr module_name, Add_Module_Options options) {
    bool const do_define = options.do_define;
    for (int n = smallsize(strlen(module_name)); n != 0;) {
        uint64_t hashvalue = hash(module_name, n);
        afor (i, build->modules_header) {
            if (build->module_name_hashes[i] == hashvalue) {
                if (0 == strncmp(build->modules[i].name, module_name, n)) {
                    if (!do_define) {
                        printf("MMM: warning: trying to add module '%s' that's already been added! while interpreting %s\n", module_name, interpreter->filename);
                    }

                    if (do_define) {
                        if (build->modules[i].is_defined) {
                            printf("Error: module '%s' has already been defined!\n", module_name);
                        }
                        build->modules[i].is_defined = true;
                    }
                    
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
        build->modules[build->modules_header.size] = (Module) { .name = p, .is_defined = do_define };
        build->module_name_hashes[build->modules_header.size] = hashvalue;
        build->modules_header.size++;

        break;
    }
}

int
chars_matches_keyword(char const *keyword, Charbuf const chars) {
    // @todo @slow
    size_t n = strlen(keyword);
    if (chars.header.size != n) return 0;
    return 0 == strncmp(chars.data, keyword, n);
}

int
token_matches_keyword(char const *keyword, Token tok, Lexer *lexer) {
    // @todo @slow
    int n = smallsize(strlen(keyword));
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
    // @todo @slow O(n2) for now
    int key_n = smallsize(strlen(key));
    afor (i, self->variables.header) {
        if (self->variables.names_len[i] != key_n)
            continue;
        if (strncmp(self->variables.names[i], key, key_n))
            continue;
        return smallsize(i);
    }
    return self->variables.header.size;
}

typedef struct VariableLookup {
  lstr value;
  bool is_recursive;
  bool empty_because_undefined;
} VariableLookup;

VariableLookup
lookup_variable(Interpreter *self, lstr key) {
    if (self->variables.header.size == 0) goto empty_variable;

    size_t i = lookup_variable_index(self, key);

    if (i == self->variables.header.size) goto empty_variable;

    return (VariableLookup){
        .value = self->variables.values[i],
        .is_recursive = self->variables.is_recursive[i]
    };

empty_variable:
    // > Most variable names are considered to have the empty string as a value if you have never set them. 
    return (VariableLookup) { 
        .value = "",
        .empty_because_undefined = true,
    };
}

VariableLookup
lookup_namespaced_variable(Interpreter *self, /* borrowed */ Charbuf namespace_name, lstr suffix) {
    int truncation_point = namespace_name.header.size;

    chars_push_nstr(&namespace_name, strlen(suffix) + 1, suffix);
    
    VariableLookup result = lookup_variable(self, (char const*)namespace_name.data);
    
    namespace_name.header.size = truncation_point;

    return result;
}

void
debug_print_variable_lookup(lstr name, VariableLookup x) {
    printf("\t%s = %s%s\n", name, x.value, x.is_recursive ? " (recursive)" : "");
}

char *
Interpreter_strdup(Interpreter *self, lstr x) {
    (void) self; // for now there is no arena.
    return _strdup(x);
}

void set_variable(Interpreter *self, lstr key, lstr value, int is_recursive) {
    assert(key);
    size_t i = lookup_variable_index(self, key);
    if (i != self->variables.header.size)
        assert_index(self->variables.header, i);
    
    if (i == self->variables.header.size && i >= self->variables.header.capacity) {
        int old_cap = self->variables.header.capacity;
        int new_cap = align_up(old_cap + old_cap + 1, 16);
        self->variables.names = recallocz(self->variables.names, old_cap, new_cap, sizeof self->variables.names[0]);
        self->variables.names_len = recallocz(self->variables.names_len, old_cap, new_cap, sizeof self->variables.names_len[0]);
        self->variables.values = recallocz(self->variables.values, old_cap, new_cap, sizeof self->variables.values[0]);
        self->variables.is_recursive = recallocz(self->variables.is_recursive, old_cap, new_cap, sizeof self->variables.is_recursive[0]);
        self->variables.header.capacity = new_cap;
    }
    if (i == self->variables.header.size) { // new name
        self->variables.header.size++;
        size_t n = strlen(key);
        self->variables.names_len[i] = smallsize(n);
        self->variables.names[i] = Interpreter_strdup(self, key);
        self->variables.is_recursive[i] = (char)is_recursive;
    }
    char *old_value = self->variables.values[i];
    self->variables.values[i] = Interpreter_strdup(self, value ? value : "");
    free(old_value);
}

void
build_define_module(Interpreter *self, /*owned*/ Module2 module) {
    assert(module.name.data[module.name.header.size] == 0);

    // @todo should not warn if the module already exists
    build_add_module(self->build, self, module.name.data, (Add_Module_Options){ .do_define = true });

    if (g_program_options.emit_debug_log) {
        printf("Adding module '%*s'\n", module.name.header.size, module.name.data);
    }
    aadd(self->modules.header, self->modules.data, module);
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
    afor (i, self->variables.header) {
        free(self->variables.names[i]);
        free(self->variables.values[i]);
    }
    free(self->variables.names);
    free(self->variables.names_len);
    free(self->variables.values);
    memset(&self->variables, 0, sizeof self->variables);
}

typedef struct Rule_Context {
    bool in_rule;
} Rule_Context;

// @todo this needs to accept expressions with variables in the arguments,
// otherwise it will stop at the first encountered )
int
interpret_function_argument(Interpreter *self, Charbuf *result) {
    Lexer *lexer = self->lexer;
    Token tok = { 0 };
    while (lexer->pos < lexer->endpos) {
        int old_pos = lexer->pos;
        tok = next_token(lexer);
        if (matches_char(tok, ',', lexer)) {
            lexer_rewind(lexer, old_pos); // instead we could return the token
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


bool
interpret_function_generic(Interpreter *interpreter, Charbuf function_name, size_t functions_count, Mk_Function const * functions, char const * prefix_for_logging, Rule_Context context) {
    Lexer *lexer = interpreter->lexer;
    
    if (!context.in_rule) {
        for (size_t i = 0; i < functions_count; i++) {
            if (chars_matches_keyword(functions[i].name, function_name)) {
                Mk_Function_Options flags = unpack_mk_function_flags(functions[i].flags);

                printf("%s: found function call to %*s\n", prefix_for_logging, function_name.header.size, function_name.data);

                if (flags.defines_module) {
                    Token tok = next_token(lexer);
                    if (!matches_char(tok, ',', lexer)) {
                        goto defines_module_not_valid;
                    }
                    Charbuf arg = { 0, };
                    if (!interpret_function_argument(interpreter, &arg)) {
                        goto defines_module_not_valid;
                    }
                    build_define_module(interpreter, 
                               (Module2){ .name = arg, .flags = pack_mk_function_flags(flags) });
                    
                    goto define_module_other_args;

                    defines_module_not_valid:
                        print_error_at(lexer, tok.pos);

                    define_module_other_args:
                        assert(1);
                }
                
                printf("%s: args: ", prefix_for_logging);
                Token tok = next_token(lexer);
                printf("XXX: %d ", tok.kind);
                printf(" got '%.*s'\n", tok.len, text(tok, lexer));
                while(lexer->pos < lexer->endpos && matches_char(tok, ',', lexer)) {
                    Charbuf arg = { 0, };
                    if (interpret_function_argument(interpreter, &arg)) {
                        printf("'%*s', ", arg.header.size, arg.data);
                    }
                    chars_free(&arg);
                
                    tok = next_token(lexer);
                }
                printf("\n");

                print_context_at(lexer, lexer->toplevel_pos, prefix_for_logging);
                printf("\n");

                return 1;
            }
        }
    }

    return 0;}

bool
interpret_ysr_function(Interpreter *interpreter, Charbuf function_name, Rule_Context context) {
    size_t function_names_count = sizeof Ysr_Functions / sizeof Ysr_Functions[0];
    Mk_Function const * function_names = Ysr_Functions;
    return interpret_function_generic(interpreter, function_name, function_names_count, function_names, "YFYF", context);
}

bool
interpret_ln2_function(Interpreter *interpreter, Charbuf function_name, Rule_Context context) {
    size_t function_names_count = sizeof Ln2_Functions / sizeof Ln2_Functions[0];
    Mk_Function const * function_names = Ln2_Functions;
    return interpret_function_generic(interpreter, function_name, function_names_count, function_names, "LN2LN2", context);
}


// either variable or function

typedef struct Variable_Or_Function
{
    bool success;
    enum {
        VOF_DoubleDollar,
        VOF_Variable,
        VOF_Function,
        VOF_Automatic,
    } kind;
} Variable_Or_Function;


Variable_Or_Function
interpret_variable_or_function(Interpreter *interpreter, Charbuf *result, Rule_Context context) {
    Lexer *lexer = interpreter->lexer;
    Token tok;
    tok = next_token(lexer);
    if (matches_char(tok, '$', lexer)) {
        chars_push_nstr(result, 1, "$");
        // @todo I'm not sure this is legit
        return (struct Variable_Or_Function){ .success = true, .kind = VOF_DoubleDollar };
    } else if (context.in_rule && matches_char(tok, '@', lexer) ||
        matches_char(tok, '%', lexer) ||
        matches_char(tok, '<', lexer) ||
        matches_char(tok, '?', lexer) ||
        matches_char(tok, '^', lexer) ||
        matches_char(tok, '+', lexer) ||
        matches_char(tok, '|', lexer) ||
        matches_char(tok, '*', lexer)) {
        // 10.5.3 Automatic Variables
        return (struct Variable_Or_Function) { .success = true, .kind = VOF_Automatic };
    }
    if (!matches_char(tok, '(', lexer)) {
        interpreter_error(interpreter, "expecting ( at start of function or variable reference", tok);
        return (struct Variable_Or_Function){ .success = false };
    }
    Charbuf variable_name = { 0 };
    while (lexer->pos < lexer->endpos) {
        tok = next_token(lexer);
        if (matches_char(tok, ')', lexer) ||
            matches_char(tok, ' ', lexer)) {
            break;
        } else if (matches_char(tok, '$', lexer)) { 
            // references can be nested. @todo although I notice that this doesn't mean they're evaluated when it comes to a function being called, so I'm not sure this is the right structure here.
            Charbuf subreference_result = { 0 };
            Variable_Or_Function subreference = interpret_variable_or_function(interpreter, &subreference_result, context);
            if (!subreference.success)
                return (struct Variable_Or_Function) { .success = false };
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
            if (g_program_options.emit_debug_log) {
                print_context_at(lexer, tok.pos, "FFF");
                printf("$(%.*s...) control function\n", variable_name.header.size, variable_name.data);
            }
        }
        else if (chars_matches_keyword("shell", variable_name)) {
            printf("$(%.*s...) shell function\n", variable_name.header.size, variable_name.data);
        }
        else if (chars_matches_keyword("addprefix", variable_name) ||
            chars_matches_keyword("addsuffix", variable_name)) {
            printf("$(%.*s...) text function\n", variable_name.header.size, variable_name.data);
        }
        else if (chars_matches_keyword("firstword", variable_name)) {
            printf("$(%.*s...) list function\n", variable_name.header.size, variable_name.data);
        }
        else if (chars_matches_keyword("realpath", variable_name) ||
            chars_matches_keyword("dir", variable_name) ||
            chars_matches_keyword("abspath", variable_name)) {
            printf("$(%.*s...) path function\n", variable_name.header.size, variable_name.data);
        }
        else if (chars_matches_keyword("patsubst", variable_name)) {
            printf("$(%.*s...) substitution function\n", variable_name.header.size, variable_name.data);
        }
        else if (chars_matches_keyword("eval", variable_name)) {
            // no-op
            print_context_at(lexer, tok.pos, "FFF");
            printf("$(%.*s...) eval, ignored/not implemented\n", variable_name.header.size, variable_name.data);
        }
        // user-defined
        else if (chars_matches_keyword("call", variable_name)) {
            int function_pos = tok.pos;
            Charbuf user_function_name = { 0 };
            // @todo this appears to fail with to-lib-$(ARCH):
            if (!interpret_function_argument(interpreter, &user_function_name)) {
                print_error_at(lexer, lexer->pos);
            }
            
            if (interpret_ysr_function(interpreter, user_function_name, context)) {
                // success
            } else if (interpret_ln2_function(interpreter, user_function_name, context)) {
                // success
            } else {
                print_context_at(lexer, function_pos, "FFF");
                printf("call to user defined function '%s'\n", user_function_name.data);
            }
            
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
                Variable_Or_Function subreference = interpret_variable_or_function(interpreter, &subreference_result, context);
                if (!subreference.success)
                    (struct Variable_Or_Function) { .success = false };
                chars_push_nstr(&arguments, subreference_result.header.size, subreference_result.data);
                chars_free(&subreference_result);
            } else {
                chars_push_nstr(&arguments, tok.len, text(tok, lexer));
            }
        }
        return (struct Variable_Or_Function) { .success = true, .kind = VOF_Function };
    }

    if (!matches_char(tok, ')', lexer)) {
        interpreter_error(interpreter, "expected ) at end of function or variable reference", tok);
        return (struct Variable_Or_Function) { .success = false };
    }

    VariableLookup var = lookup_variable(interpreter, variable_name.data);
    if (!var.value) {
        printf("error: could not find value of variable '%s'\n", variable_name.data);
        return (struct Variable_Or_Function) { .success = false };
    }
    chars_push_nstr(result, strlen(var.value), var.value);
    return (struct Variable_Or_Function) { .success = true, .kind = VOF_Variable };
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
            if (!interpret_variable_or_function(interpreter, &reference_value, (Rule_Context){0,}).success) {
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

void interpreter_load_file(Interpreter *interpreter, char *filename, int is_optional, Error *error);

void
interpret_include_find_and_load_file(Interpreter *interpreter, char *filename_spec, int is_optional) {
    // @todo implement lookup in various include-dirs.
    
    int must_ignore_include_file = 
        0 == strcmp(filename_spec, "./config.mk") ||
        0 == strcmp(filename_spec, "ysr.mk") ||
        0 == strcmp(filename_spec, "$(realpath $(YSR.libdir))/functions/functions.mk") ||
        0 == strcmp(filename_spec, "$(realpath $(YSR.libdir))/languages/gcc/rules.mk") ||
        0 == strcmp(filename_spec, "$(realpath $(YSR.libdir))/languages/gcc/flags.mk") ||
        0 == strcmp(filename_spec, "$(realpath $(YSR.libdir))/templates/target-rules.mk");
    
    if (must_ignore_include_file && !is_optional)
    {
        if (g_program_options.emit_debug_log) {
            printf("III: ignoring include because file in ignore list: %s\n", filename_spec);
        }
        return;
    }

    typedef struct IncludeDir { size_t n; char const *path; } IncludeDir;

    IncludeDir include_dirs[] = {
        { 0, "" }, // naked path, to load absolute paths
        { strlen(interpreter->dirname), interpreter->dirname }, // @todo dumb to recalculate strlen each time here,
        { strlen(interpreter->project->ysrlibdir), interpreter->project->ysrlibdir },
    };

    Error error = { 0 };
    Charbuf path = { 0 };

    for (IncludeDir *p = &include_dirs[0], *l = &include_dirs[sizeof include_dirs / sizeof include_dirs[0]];
        p != l;
        p++)
    {
        buf_reset(&path.header);
        chars_push_nstr(&path, p->n, p->path);
        if (path.header.size > 0) {
            char delimiter = path.data[path.header.size - 1];
            if ((delimiter != '/') && (delimiter != '\\')) {
                chars_push_nstr(&path, 1, "/");
            }
        }
        chars_push_nstr(&path, strlen(filename_spec), filename_spec);

        interpreter_load_file(interpreter, path.data, is_optional, &error);
        if (error.code == ErrorCode_FileNotFound && p + 1 != l) {
            error_clear(&error);
            continue;
        }
        break;
    }
    
    if (error.code != ErrorCode_None) {
        printf("error: while including file %s, could not be found in any of the include directories, last path tried was %s\n", filename_spec, path.data);
        error_clear(&error);
    }

    error_free(&error);
    chars_free(&path);
}

void
interpret_include(Interpreter *interpreter, int is_optional) {
    if (g_program_options.emit_debug_log) {
        printf("III: include directive here in this line: ");
        print_context_at(interpreter->lexer, interpreter->lexer->pos, 0);
    }

    expects_space(interpreter);
    buf_reset(&interpreter->tmpbuf.header);
    if (!interpret_filename(interpreter, &interpreter->tmpbuf)) {
        return;
    }
    interpret_include_find_and_load_file(interpreter, interpreter->tmpbuf.data, is_optional);

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
        interpret_include_find_and_load_file(interpreter, interpreter->tmpbuf.data, is_optional);
    }
}

int
interpret_word(Interpreter *self, Token tok, Charbuf *result, Rule_Context context) {
    Lexer *lexer = self->lexer;
    if (matches_char(tok, '$', lexer)) {
        Charbuf reference_value = { 0 };
        if (!interpret_variable_or_function(self, &reference_value, context).success) {
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

bool
interpret_toplevel_function(Interpreter *self, Token tok) {
    Lexer *lexer = self->lexer;
    int initial_pos = lexer->pos;
    if (matches_char(tok, '$', lexer)) {
        Charbuf reference_value = { 0 };
        Variable_Or_Function const expected_function = interpret_variable_or_function(self, &reference_value, (Rule_Context) { 0, });
        if (!(expected_function.success && expected_function.kind == VOF_Function)) {
            chars_free(&reference_value);
            lexer_rewind(lexer, initial_pos);
            return false;
        }

        chars_free(&reference_value);
        return true;
    }
    return false;
}

int
interpret_assignment(Interpreter *self, Token first_token) {
    Lexer *lexer = self->lexer;
    Token tok;

    int initial_pos = lexer->pos;

    do {
        tok = next_token(lexer);
    } while (matches_space(tok, lexer));

    if (tok.kind == TokenKind_Assignment) {
        if (g_program_options.emit_debug_log) {
            printf("AAA: assignment found.");
            print_context_at(lexer, tok.pos, "AAA");
            printf(" variable name is '%.*s'", first_token.len, text(first_token, lexer));
        }

        bool all_caps = true;
        for (int i = 0; all_caps && i < first_token.len; i++) {
            char c = lexer->input[first_token.pos + i];
            if ('a' <= c && c <= 'z')
                all_caps = false;
        }
        if (g_program_options.emit_debug_log)
            if (all_caps)
                printf(" (parameter for implicit rules or user-overridable parameter)");
        int c = text(tok, lexer)[0];

        if (g_program_options.emit_debug_log)
            printf(" flavor:");

        int is_recursive = c == ':' ? 0 : 1;
        switch (c) {
            break; case ':': if (g_program_options.emit_debug_log) printf(" simply expanded\n");
            break; case '=': if (g_program_options.emit_debug_log) printf(" recursively expanded\n");
            break; case '?': if (g_program_options.emit_debug_log) printf(" conditional, recursively expanded\n");
            break; case '+': if (g_program_options.emit_debug_log) printf(" appending (flavor unchanged)\n");
            break; default: {
                print_error_at(lexer, first_token.pos);
                printf("  unknown type (%c)\n", c);
                return 0;
            }
        }

        bool success = true;
        // consume value.
        consume_whitespace(lexer);
        Charbuf value = { 0 };
        if (!is_recursive) {
            // expand simply expanded variables
            while (lexer->pos < lexer->endpos) {
                tok = next_token(lexer);
                if (matches_eol(tok))
                    break;
                if (!interpret_word(self, tok, &value, (Rule_Context) { 0, })) {
                    success = false;
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
        if (!success) return 0;

        // @todo @wip set variables, taking into account the type of the variable and the assignment operator.
        Charbuf variable_name = { 0 };
        chars_push_nstr(&variable_name, first_token.len, text(first_token, lexer));
        set_variable(self, variable_name.data, value.data, is_recursive);

        if (chars_matches_keyword("1", value)) {
            // likely a module?
            printf("MMM: are you a module? %s\n", variable_name.data);
            if (self->build)
                build_add_module(self->build, self, variable_name.data, (Add_Module_Options){0,});
        }


        chars_free(&variable_name);
        chars_free(&value);

        return 1;
    } else {
        // failed matching
        lexer_rewind(lexer, initial_pos);
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
        if (!interpret_word(interpreter, tok, result, (Rule_Context) { .in_rule = true })) {
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

    // 1. target
    if (!interpret_rule_target(self, &target_buf))
        goto not_a_rule;

    if (!target_buf.data) {
        printf("RRR: a rule but without a target? That sounds fishy unless it's because there's a variable in here\n");
        print_context_at(lexer, first_token.pos, "RRR");
        printf("here\n");
    }
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

    // 2. prerequisites
    while (lexer->pos < lexer->endpos) {
        tok = next_token(lexer);
        if (matches_eol(tok))
            break;
    }

    // 3. recipes
    lexer->expects_recipe = true;
    while (lexer->pos < lexer->endpos) {
        int old_pos = lexer->pos;
        tok = next_token(lexer);
        if (tok.kind != TokenKind_Recipe) {
            lexer_rewind(lexer, old_pos);
            break;
        }
        tok = next_token(lexer);
        if (!matches_eol(tok)) {
            printf("RRR: error on recipe, not ending with end-of-line\n");
            print_context_at(lexer, tok.pos, "RRR");
            break;
        }
    }
    lexer->expects_recipe = false;

    lstr target_name = target_buf.data;
    if (g_program_options.emit_debug_log) {
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
        lexer->toplevel_pos = tok.pos;

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
            } else if (interpret_toplevel_function(interpreter, tok)) {
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

void
interpreter_load_file(Interpreter *interpreter, char *filename, int is_optional, Error *error) {
    Lexer *old_lexer = interpreter->lexer;
    char *old_filename = interpreter->filename;
    char *old_dirname = interpreter->dirname;

    size_t num_bytes = 0;
    char *file_content = read_whole_file(filename, &num_bytes, error);
    if (error->code != ErrorCode_None) {
        // @todo Logic can be moved to the caller.
        if (is_optional) {
            error_clear(error);
            return; // silent
        }
        return;
    }

    // Switch interpreter to work on this file as its current file:
    interpreter->filename = filename;
    { // Calculate dirname
        char * const f = strdup(interpreter->filename);
        char *l = f;
        for (char *p = f; *p; p++) {
            char c = *p;
            if ((c == '/') || (c == '\\')) { l = p; }
        }
        l[1] = '\0';
        interpreter->dirname = f;
    }

    Lexer lexer = { 
        .filename = filename,
        .input = file_content, 
        .endpos = smallsize(num_bytes), 
        0
    };
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
    interpreter->dirname = old_dirname;
    interpreter->lexer = old_lexer;
}

void
process_ysr_file(Project *project, char *filename) {
    Build build = { 0, };
    Interpreter interpreter = { 0, };

    interpreter.project = project;

    set_variable(&interpreter, "TOP", project->topdir, 0);
    set_variable(&interpreter, "YSR.project.file", project->projectfile, 0);
    set_variable(&interpreter, "YSR.libdir", project->ysrlibdir, 0);
    set_variable(&interpreter, "HOST_CONFIG_MK", project->host_config_mk, 0);

    set_variable(&interpreter, "DEST", "<dest>", 1);
    set_variable(&interpreter, "YSR.bin", "ysr", 0);

    build_create(&build);

    interpreter.build = &build;

    Error error = { 0 };
    interpreter_load_file(&interpreter, filename, 0, &error);

    // Inspect all modules and process them:
    afor(i, interpreter.modules.header) {
        Module2 it = interpreter.modules.data[i];
        printf("Module '%*s'\n", it.name.header.size, it.name.data);

        Charbuf temp = { 0, };
        chars_push_nstr(&temp, it.name.header.size, it.name.data);

        VariableLookup iplug = lookup_namespaced_variable(&interpreter, temp, "_IPLUG");
        if (iplug.empty_because_undefined) {
            printf("Error, expected value for %s\n", temp.data);
        } else {
            debug_print_variable_lookup(temp.data, iplug);
        }

        VariableLookup res = lookup_namespaced_variable(&interpreter, temp, "_RES");
        if (iplug.empty_because_undefined) {
            printf("Error, expected value for %s\n", temp.data);
        } else {
            debug_print_variable_lookup(temp.data, res);
        }

        VariableLookup objs = lookup_namespaced_variable(&interpreter, temp, "_OBJS");
        debug_print_variable_lookup(temp.data, objs);

        VariableLookup deps = lookup_namespaced_variable(&interpreter, temp, "_DEPS");
        debug_print_variable_lookup(temp.data, deps);

        chars_free(&temp);
    }
    
    error_free(&error);

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
        // we'll take this project as our example for now -2024-05
        //
        "h:/ln2/trunk/plugins/Gordia/Makefile",
        "h:/ln2/trunk/apps/examples/Makefile.ysr",
    };
    size_t num_filenames = sizeof filenames_data / sizeof filenames_data[0];

    for (size_t i = 0; i < num_filenames; i++) {
        printf("----\nhello %s\n", filenames_data[i]);
        process_ysr_file(&project, filenames_data[i]);
    }

    return 0;
}
