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
    char const* topdir /* TOP variable */;
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

typedef struct Token {
    int pos, len;
} Token;

typedef struct Parser {
    Lexer *lexer;
    Charbuf tmpbuf;
} Parser;


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
print_error_at(Lexer *lexer, int pos) {
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
    printf("error: ");
    printf("%.*s\n", line_end_pos - line_start_pos, &lexer->input[line_start_pos]);
    printf("error: ");
    printf("%*s^", pos - line_start_pos, "");
    assert(0);
}

void
lexer_rewind(Lexer *lexer, int pos) {
    assert(0 <= pos && pos <= lexer->endpos);
    lexer->pos = pos;
}

int
lexer_expect_char(Lexer *lexer, char* context, char expected_char, char const* optional_name_of_expected_char) {
    char c = lexer->input[lexer->pos];
    int success = c == expected_char;
    if (!success) {
        print_error_at(lexer, lexer->pos);

        printf("error during lexing of %s at byte %d, expected ", context, lexer->pos);
        if (optional_name_of_expected_char) {
            printf("%s", optional_name_of_expected_char);
        } else {
            printf("'%c'", expected_char);
        }
        printf(" got '%c' (%d)\n", c, c);
    } else {
        lexer->pos++;
    }
    return success;
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
    char const* p = lexer->input;
    while (1) {
        char c = p[lexer->pos++];
        if (c == '\n') {
            break;
        }
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
                Token escaped_eol = { .pos = lexer->pos };
                expect_eol(lexer);
                terminate_token(lexer, &escaped_eol);
                return escaped_eol;
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
                Token newline = { .pos = lexer->pos };
                if (expect_eol(lexer)) {
                    lexer->logical_line++;
                    terminate_token(lexer, &newline);
                    return newline;
                }
            } break;
            case '\n': {
                Token newline = { .pos = lexer->pos };
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
            case '?': case '!': case '+':
                Token assign_token = { .pos = lexer->pos++ };
                c = p[lexer->pos];
                if (lexer_expect_char(lexer, "assignment", '=', 0)) {
                    terminate_token(lexer, &assign_token);
                    return assign_token;
                }
            case '$': case '(': case ')': {
                // these tokens stand for themselves.
                Token char_token = { .pos = lexer->pos++, .len = 1 };
                return char_token;               
            } break;
            default:
                if (is_word_at_char(c)) {
                    Token word_token = { .pos = lexer->pos++ };
                    consume_word(lexer);
                    terminate_token(lexer, &word_token);
                    return word_token;
                } else if (is_recipe_at_char(lexer, c)) {
                    if (lexer->pos == 0 || lexer->input[lexer->pos - 1] == '\n') {
                        Token recipe_token = { .pos = lexer->pos, };
                        consume_line(lexer);
                        terminate_token(lexer, &recipe_token);
                        return recipe_token;
                    } else {
                        Token char_token = { .pos = lexer->pos++, .len = 1 };
                        return char_token;
                    }            
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
matches_eol(Token tok, Lexer *lexer) {
    return lexer->input[tok.pos] == '\n';
}

int
matches_char(Token tok, char c, Lexer *lexer) {
    return tok.len == 1 && lexer->input[tok.pos] == c;
}

lstr text(Token tok, Lexer *lexer) {
    return &lexer->input[tok.pos];
}


void
parser_error(Parser *parser, char* context, Token tok) {
    Lexer *lexer = parser->lexer;
    print_error_at(lexer, tok.pos);

    printf("error during parsing of %s at byte %d,", context, lexer->pos);
    printf(" got '%.*s'\n", tok.len, text(tok, lexer));    
}

int
matches_space(Token tok, Lexer *lexer) {
    return matches_char(tok, ' ', lexer);
}

int
expects_space(Parser *parser) {
    Lexer *lexer = parser->lexer;
    Token tok = next_token(lexer);
    if (!matches_space(tok, lexer)) {
        parser_error(parser, "expecting space", tok);
        return 0;
    }
    return 1;
}

void
parser_free(Parser *parser) {
    chars_free(&parser->tmpbuf);
}

// either variable or function
int
parse_reference(Parser *parser) {
    Lexer *lexer = parser->lexer;
    Token tok; 
    tok = next_token(lexer);
    if (!matches_char(tok, '(', lexer)) {
        parser_error(parser, "expecting ( at start of function or variable reference", tok);
        return 0;
    }
    while (lexer->pos < lexer->endpos) {
        tok = next_token(lexer);
        if (matches_char(tok, ')', lexer)) {
            break;
        }
    }
    if (!matches_char(tok, ')', lexer)) {
        parser_error(parser, "expected ) at end of function or variable reference", tok);
        return 0;
    }
    return 1;
}

void
parse_filename(Parser *parser) {
    Lexer *lexer = parser->lexer;
    int needs_evaluation = 0;
    chars_reset(&parser->tmpbuf);
    while (lexer->pos < lexer->endpos) {
        int old_pos = lexer->pos;
        Token tok = next_token(lexer);
        if (matches_eol(tok, lexer) || matches_space(tok, lexer)) {
            lexer_rewind(lexer, old_pos);
            break;
        }
        if (matches_char(tok, '$', lexer)) {
            if (!parse_reference(parser)) {
                break;
            }
            printf("Variable/function reference: '%.*s'", lexer->pos - tok.pos, &lexer->input[tok.pos]);
            needs_evaluation = 1;
        } else {
            printf("%.*s", tok.len, text(tok, lexer));
            chars_push_nstr(&parser->tmpbuf, tok.len, text(tok, lexer)); // needs eval, so let's stop building
        }
    }
    if (needs_evaluation) {
        // @todo
        printf("I do not know how to evaluate references yet");
    }
    if (!needs_evaluation) {
        printf("content of tmpbuf: %s\n", parser->tmpbuf.data);
    }
}

void
parse_include(Parser *parser) {
    printf("III: include directive here in this line: ");
    
    expects_space(parser);
    parse_filename(parser);
    
    Lexer *lexer = parser->lexer;
    while (lexer->pos < lexer->endpos) {
        Token tok = next_token(parser->lexer);
        if (matches_eol(tok, lexer)) {
            break;
        }
        if (!matches_space(tok, lexer)) {
            parser_error(parser, "expected space between the filenames of an include directive", tok);
            break;
        }
        parse_filename(parser);
    }
}

int
parse_toplevel(Parser* parser) {
    Lexer *lexer = parser->lexer;

    char const* sep = "\n";
    while (lexer->pos < lexer->endpos) {
        Token tok = next_token(lexer); // first token in the line.

        printf("%s", sep);

        // parse directives:
        if (matches_keyword("include", tok, lexer) ||
            matches_keyword("-include", tok, lexer) ||
            matches_keyword("sinclude", tok, lexer)) {
            parse_include(parser);
            return 1;
        } else {
            if (text(tok, lexer)[0] != '\n') {
                printf("([%.*s]@%d)", tok.len, text(tok, lexer), tok.pos);
            }
        }
        
        if (tok.pos >= lexer->endpos) { return 0; }
        sep = ", ";
    }
    return 0;
}

void
process_ysr_file(Project *proj, char *filename) {
    size_t num_bytes = 0;
    char* err = 0;
    char *file_content = read_whole_file(filename, &num_bytes, &err);
    if (err) {
        printf("error while reading file %s: %s\n", filename, err);
        return;
    }
    
    Lexer lexer = { .input = file_content, .endpos = num_bytes, 0 };
    Parser parser = { .lexer = &lexer };
    while (parse_toplevel(&parser)) {
        // continue;
    }
    printf("Stats for %s:\n", filename);
    printf("num_bytes: %d\n", num_bytes);
    printf("num_tokens: %d\n", lexer.num_tokens);
    printf("avg_byte_per_token: %f\n", 1.0 * num_bytes / lexer.num_tokens);

    parser_free(&parser);
    free(file_content);
}

int
main(void) {
    Project project = { .topdir = "h:/ln2/trunk" };
    char *filenames_data[] = {
        "h:/ln2/trunk/plugins/Gordia/Makefile",
        "h:/ysr/lib/ysr.mk",
        "h:/ln2/trunk/config.mk",
    };
    ArrayHeader filenames;
    filenames.count = sizeof filenames_data / sizeof filenames_data[0];

    for (size_t i = 0; i < filenames.count; i++) {
        printf("----\nhello %s\n", filenames_data[i]);
        process_ysr_file(&project, filenames_data[i]);
    }
    
    return 0;
}
