#include "regex_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_case(const char* pattern, bool ignore_case, const char* text, bool should_match, size_t exp_pos, size_t exp_len) {
    char err[256] = {0};
    RegexCompiled* re = regex_compile(pattern, ignore_case, err, sizeof(err));
    if (!re) {
        printf("FAIL: Failed to compile pattern '%s': %s\n", pattern, err);
        exit(1);
    }

    size_t match_pos = 0, match_len = 0;
    bool matched = regex_search_in_buffer(re, text, strlen(text), 0, false, &match_pos, &match_len);

    if (matched != should_match) {
        printf("FAIL: Pattern '%s' on text '%s' expected %d but got %d\n",
               pattern, text, should_match, matched);
        regex_free(re);
        exit(1);
    }

    if (matched && should_match) {
        if (match_pos != exp_pos || match_len != exp_len) {
            printf("FAIL: Pattern '%s' on text '%s' expected pos=%zu len=%zu, got pos=%zu len=%zu\n",
                   pattern, text, exp_pos, exp_len, match_pos, match_len);
            regex_free(re);
            exit(1);
        }
    }

    printf("PASS: Pattern '%s' on '%s' -> matched=%d (pos=%zu, len=%zu)\n",
           pattern, text, matched, match_pos, match_len);
    regex_free(re);
}

int main() {
    printf("Running SuperSearch Regex Engine Tests...\n");

    // 1. Literal matching & case sensitivity
    test_case("hello", false, "say hello world", true, 4, 5);
    test_case("hello", true, "say HELLO world", true, 4, 5);
    test_case("foo", false, "bar baz", false, 0, 0);

    // 2. Character classes & shorthands (\d, \w, \s)
    test_case("\\d+", false, "order 12345 shipped", true, 6, 5);
    test_case("\\w+Service", false, "call UserService now", true, 5, 11);
    test_case("[0-9]{3}-[0-9]{4}", false, "phone: 555-1234 here", true, 7, 8);

    // 3. Anchors & Word boundaries
    test_case("^int\\s+\\w+", false, "int count = 0;", true, 0, 9);
    test_case("\\bcat\\b", false, "the cat and concatenate", true, 4, 3);
    test_case("end$", false, "this is the end", true, 12, 3);

    // 4. Repetition & quantifiers (*, +, ?, {m,n})
    test_case("a+b*c", false, "xxx aaabbc yyy", true, 4, 6);
    test_case("colou?r", false, "color and colour", true, 0, 5);
    test_case("x{2,4}", false, "a xxxx b", true, 2, 4);
    test_case("ab{0,2}c", false, "test ac and abbc", true, 5, 2);
    test_case("ab{0,2}c", false, "test abbc here", true, 5, 4);
    test_case("z{2,}", false, "a zz b", true, 2, 2);
    test_case("(ab|cd){2,3}", false, "xx abcdab yy", true, 3, 6);

    // 5. Alternation & grouping
    test_case("cat|dog", false, "i love my dog very much", true, 10, 3);
    test_case("(ha)+", false, "laughing hahaha aloud", true, 9, 6);

    // 6. Prefixes and jumps
    test_case("Service\\w+", false, "class ServiceManager implements", true, 6, 14);

    printf("\nALL REGEX ENGINE TESTS PASSED!\n");
    return 0;
}
