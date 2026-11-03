#include "ruby.h"
#include "ruby/encoding.h"
#include "ruby/util.h"

#ifndef HAVE_RB_LEN_T
typedef long rb_len_t;
typedef unsigned long rb_ulen_t;
# define RB_LEN_MAX LONG_MAX
#endif

#ifndef HAVE_RUBY_SCAN_DIGITS
RUBY_EXTERN unsigned long ruby_scan_digits(const char *str, ssize_t len, int base, size_t *retlen, int *overflow);
#endif
#ifndef HAVE_RUBY_HEXDIGITS_0_
RUBY_EXTERN const char ruby_hexdigits[];
#endif
#ifndef HAVE_RUBY_DIGIT36_TO_NUMBER_TABLE_0_
RUBY_EXTERN const signed char ruby_digit36_to_number_table[];
#endif
#define lower_hexdigits (ruby_hexdigits+0)
#define upper_hexdigits (ruby_hexdigits+16)
#define char_to_number(c) ruby_digit36_to_number_table[(unsigned char)(c)]

static VALUE rb_cCGI, rb_mEscape, rb_mEscapeExt;
static ID id_accept_charset;

#define HTML_ESCAPE_MAX_LEN 6

static const struct {
    uint8_t len;
    char str[HTML_ESCAPE_MAX_LEN+1];
} html_escape_table[UCHAR_MAX+1] = {
#define HTML_ESCAPE(c, str) [c] = {rb_strlen_lit(str), str}
    HTML_ESCAPE('\'', "&#39;"),
    HTML_ESCAPE('&', "&amp;"),
    HTML_ESCAPE('"', "&quot;"),
    HTML_ESCAPE('<', "&lt;"),
    HTML_ESCAPE('>', "&gt;"),
#undef HTML_ESCAPE
};

static inline void
preserve_original_state(VALUE orig, VALUE dest)
{
    rb_enc_associate(dest, rb_enc_get(orig));
}

static inline rb_len_t
escaped_length(VALUE str)
{
    const rb_len_t len = RSTRING_LEN(str);
    if (len >= RB_LEN_MAX / HTML_ESCAPE_MAX_LEN) {
        ruby_malloc_size_overflow(len, HTML_ESCAPE_MAX_LEN);
    }
    return len * HTML_ESCAPE_MAX_LEN;
}

/*
 * Fast path for optimized_escape_html.
 *
 * Strings under HTML_ESCAPE_ONE_PASS bytes are escaped in one pass into a
 * stack buffer.  Longer strings are searched for the first byte to escape,
 * and a string with none is returned by rb_str_dup.  Otherwise
 * escape_html_into copies the clean prefix at once and escapes the rest.
 *
 * The SSE2 and NEON code is ported from ruby/erb's ext/erb/escape/escape.c
 * (ruby/erb#136 and follow-ups), which took it from the json gem.  Set
 * CGI_DISABLE_SIMD when building to leave it out.
 */

#ifdef __clang__
# if __has_builtin(__builtin_ctzll)
#   define HAVE_BUILTIN_CTZLL 1
# else
#   define HAVE_BUILTIN_CTZLL 0
# endif
#elif defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3))
# define HAVE_BUILTIN_CTZLL 1
#else
# define HAVE_BUILTIN_CTZLL 0
#endif

#ifdef CGI_ENABLE_SIMD
#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(__x86_64) || defined(_M_X64) || defined(_M_AMD64)
#ifdef HAVE_X86INTRIN_H
#include <x86intrin.h>
#define HAVE_SIMD 1
#define HAVE_SIMD_SSE2 1
#endif
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define HAVE_SIMD 1
#define HAVE_SIMD_NEON 1
#include <arm_neon.h>
#endif
#endif /* CGI_ENABLE_SIMD */

/*
 * Strings shorter than this skip the search and are escaped in one pass
 * into a stack buffer.  With SIMD, the search pays off on longer strings
 * that need no escaping.  Without SIMD it costs about as much as escaping,
 * so the limit only sizes the stack buffer, which stays under the 1 KB that
 * ALLOCV already put on the stack.
 */
#ifdef HAVE_SIMD
# define HTML_ESCAPE_ONE_PASS 64
#else
# define HTML_ESCAPE_ONE_PASS 128
#endif

#ifdef HAVE_SIMD
static inline unsigned int
trailing_zeros64(uint64_t input)
{
#if HAVE_BUILTIN_CTZLL
    return __builtin_ctzll(input);
#else
    unsigned int trailing_zeros = 0;
    uint64_t temp = input;
    while ((temp & 1) == 0 && temp > 0) {
        trailing_zeros++;
        temp >>= 1;
    }
    return trailing_zeros;
#endif
}
#endif

#ifdef HAVE_SIMD_SSE2
/* Bit i is set if p[i] is one of ' " & < > */
static inline uint64_t
escape_bitmap16(const unsigned char *p)
{
    const __m128i bytes = _mm_loadu_si128((__m128i const *)p);
    const __m128i match1 = _mm_cmpeq_epi8(bytes, _mm_set1_epi8('\''));
    const __m128i match2 = _mm_cmpeq_epi8(bytes, _mm_set1_epi8('"'));
    const __m128i match3 = _mm_cmpeq_epi8(bytes, _mm_set1_epi8('&'));
    const __m128i match4 = _mm_cmpeq_epi8(bytes, _mm_set1_epi8('<'));
    const __m128i match5 = _mm_cmpeq_epi8(bytes, _mm_set1_epi8('>'));

    const __m128i mask1 = _mm_or_si128(match1, match2);
    const __m128i mask2 = _mm_or_si128(match3, match4);
    const __m128i mask3 = _mm_or_si128(mask1, match5);
    const __m128i matches = _mm_or_si128(mask2, mask3);

    return (unsigned int)_mm_movemask_epi8(matches);
}
#endif /* HAVE_SIMD_SSE2 */

#ifdef HAVE_SIMD_NEON
/*
 * The escapable character with each low nibble, if any.  Index 0 holds
 * '\'' instead of 0 so that NUL bytes do not match.
 */
static const uint8x16_t escape_char_by_low_nibble = {
    '\'', 0,    '"',  0,
    0,    0,    '&',  '\'',
    0,    0,    0,    0,
    '<',  0,    '>',  0,
};

/*
 * Returns 0xFF in each lane whose byte needs escaping and 0x00 in the rest,
 * by looking up the character for each byte's low nibble and comparing.
 * For "<br />" (3c 62 72 20 2f 3e), the low nibbles (0c 02 02 00 0f 0e)
 * look up (3c 22 22 27 00 3e), so only the first and last lanes match.
 */
static inline uint8x16_t
neon_escape_matches(const uint8x16_t bytes)
{
    const uint8x16_t low_nibbles = vandq_u8(bytes, vdupq_n_u8(0x0F));
    const uint8x16_t looked_up = vqtbl1q_u8(escape_char_by_low_nibble, low_nibbles);
    return vceqq_u8(looked_up, bytes);
}

static inline uint64_t
neon_matches_to_bitmap16(const uint8x16_t matches)
{
    static const uint8x16_t bit_mask = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
    };

    uint8x16_t folded = vandq_u8(matches, bit_mask);
    folded = vpaddq_u8(folded, folded);
    folded = vpaddq_u8(folded, folded);
    folded = vpaddq_u8(folded, folded);

    return vgetq_lane_u16(vreinterpretq_u16_u8(folded), 0);
}

static inline uint64_t
neon_matches_to_bitmap64(const uint8x16_t m0, const uint8x16_t m1, const uint8x16_t m2, const uint8x16_t m3)
{
    static const uint8x16_t bit_mask = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
    };

    const uint8x16_t t0 = vandq_u8(m0, bit_mask);
    const uint8x16_t t1 = vandq_u8(m1, bit_mask);
    const uint8x16_t t2 = vandq_u8(m2, bit_mask);
    const uint8x16_t t3 = vandq_u8(m3, bit_mask);

    uint8x16_t folded = vpaddq_u8(vpaddq_u8(t0, t1), vpaddq_u8(t2, t3));
    folded = vpaddq_u8(folded, folded);

    return vgetq_lane_u64(vreinterpretq_u64_u8(folded), 0);
}

/* Bit i is set if p[i] is one of ' " & < > */
static inline uint64_t
escape_bitmap16(const unsigned char *p)
{
    return neon_matches_to_bitmap16(neon_escape_matches(vld1q_u8(p)));
}
#endif /* HAVE_SIMD_NEON */

/* Returns the first byte in [p, end) that needs escaping, or end. */
static inline const unsigned char *
find_first_escape(const unsigned char *p, const unsigned char *end)
{
#ifdef HAVE_SIMD
    if (end - p >= 16) {
        const unsigned char *const last = end - 16;
        uint64_t bitmap;

        /* Check the first 16 bytes alone, since escapes often come early. */
        bitmap = escape_bitmap16(p);
        if (bitmap) {
            return p + trailing_zeros64(bitmap);
        }
        p += 16;

# ifdef HAVE_SIMD_NEON
        while (end - p >= 64) {
            const uint8x16_t m0 = neon_escape_matches(vld1q_u8(p +  0));
            const uint8x16_t m1 = neon_escape_matches(vld1q_u8(p + 16));
            const uint8x16_t m2 = neon_escape_matches(vld1q_u8(p + 32));
            const uint8x16_t m3 = neon_escape_matches(vld1q_u8(p + 48));
            bitmap = neon_matches_to_bitmap64(m0, m1, m2, m3);

            if (bitmap) {
                return p + trailing_zeros64(bitmap);
            }
            p += 64;
        }
# endif
# ifdef HAVE_SIMD_SSE2
        while (end - p >= 64) {
            bitmap = escape_bitmap16(p) | escape_bitmap16(p + 16) << 16 |
                     escape_bitmap16(p + 32) << 32 | escape_bitmap16(p + 48) << 48;
            if (bitmap) {
                return p + trailing_zeros64(bitmap);
            }
            p += 64;
        }
# endif
        while (p < last) {
            bitmap = escape_bitmap16(p);
            if (bitmap) {
                return p + trailing_zeros64(bitmap);
            }
            p += 16;
        }
        /* The last 16 bytes.  Any that overlap the blocks above are clean. */
        bitmap = escape_bitmap16(last);
        return bitmap ? last + trailing_zeros64(bitmap) : end;
    }
#endif
    /* Unrolled by hand to one branch per 8 bytes. */
    while (end - p >= 8) {
        if (html_escape_table[p[0]].len | html_escape_table[p[1]].len |
            html_escape_table[p[2]].len | html_escape_table[p[3]].len |
            html_escape_table[p[4]].len | html_escape_table[p[5]].len |
            html_escape_table[p[6]].len | html_escape_table[p[7]].len) {
            break;
        }
        p += 8;
    }
    while (p < end && !html_escape_table[*p].len) {
        p++;
    }
    return p;
}

/*
 * Appends c, whose escaped length is len (0 if c is clean).  The whole
 * HTML_ESCAPE_MAX_LEN-byte entry is copied in a couple of moves and dest
 * advances by len, so dest needs room for HTML_ESCAPE_MAX_LEN bytes.
 */
#define APPEND_ESCAPED(dest, c, len) do { \
    if (len) { \
        memcpy((dest), html_escape_table[c].str, HTML_ESCAPE_MAX_LEN); \
        (dest) += (len); \
    } \
    else { \
        *(dest)++ = (char)(c); \
    } \
} while (0)

/*
 * Appends the 4 bytes at p: in one move if all 4 are clean, else one at a
 * time with the lengths already looked up.  dest needs room for
 * 4 * HTML_ESCAPE_MAX_LEN bytes.  gcc and clang each compiled some
 * byte-by-byte loops into code slower than the old loop; this form is at
 * least as fast with both.
 */
#define APPEND_ESCAPED4(dest, p) do { \
    const unsigned char c0_ = (p)[0], c1_ = (p)[1]; \
    const unsigned char c2_ = (p)[2], c3_ = (p)[3]; \
    const uint8_t len0_ = html_escape_table[c0_].len; \
    const uint8_t len1_ = html_escape_table[c1_].len; \
    const uint8_t len2_ = html_escape_table[c2_].len; \
    const uint8_t len3_ = html_escape_table[c3_].len; \
    if (!(len0_ | len1_ | len2_ | len3_)) { \
        memcpy((dest), (p), 4); \
        (dest) += 4; \
    } \
    else { \
        APPEND_ESCAPED((dest), c0_, len0_); \
        APPEND_ESCAPED((dest), c1_, len1_); \
        APPEND_ESCAPED((dest), c2_, len2_); \
        APPEND_ESCAPED((dest), c3_, len3_); \
    } \
} while (0)

/*
 * Escapes [start, end) into dest and returns the end of the output.  The
 * bytes in [start, first) are clean, and first > start only for strings of
 * 16 bytes or more.  dest needs room for HTML_ESCAPE_MAX_LEN bytes per
 * input byte.
 *
 * NOINLINE because, inlined into optimized_escape_html, the compiler sees
 * the buffer's size, and with _FORTIFY_SOURCE=3 (the default on some
 * distributions) the small fixed-size memcpy calls below would become
 * __memcpy_chk calls.
 */
NOINLINE(static char *escape_html_into(char *dest, const unsigned char *start, const unsigned char *first, const unsigned char *end));

static char *
escape_html_into(char *dest, const unsigned char *start, const unsigned char *first, const unsigned char *end)
{
    const unsigned char *cstr = first;
    const size_t prefix = first - start;

    /*
     * Copy the clean prefix: one memcpy call if it is long, else 16-byte
     * moves, which cost less than a call.  The last move ends at first.  A
     * prefix under 16 bytes is copied as 16, which the input has (see
     * above) and dest has room for, and the extra bytes are overwritten.
     */
    if (prefix >= 256) {
        memcpy(dest, start, prefix);
    }
    else if (prefix >= 16) {
        size_t i;

        for (i = 0; i + 16 < prefix; i += 16) {
            memcpy(dest + i, start + i, 16);
        }
        memcpy(dest + prefix - 16, first - 16, 16);
    }
    else if (prefix) {
        memcpy(dest, start, 16);
    }
    dest += prefix;

#ifdef HAVE_SIMD
    {
        /*
         * While 64 or more bytes are left, take 16 at a time: copy a clean
         * block whole and escape any other 4 bytes at a time.  After a block
         * with 5 or more escapes, escape the next one without searching it,
         * since in text that dense a clean block is rare.  On
         * the last 63 bytes or fewer, the search costs more than it saves.
         */
        int dense = 0;

        while (end - cstr >= 64) {
            if (!dense && !escape_bitmap16(cstr)) {
                memcpy(dest, cstr, 16);
                dest += 16;
            }
            else {
                char *const block = dest;

                APPEND_ESCAPED4(dest, cstr);
                APPEND_ESCAPED4(dest, cstr + 4);
                APPEND_ESCAPED4(dest, cstr + 8);
                APPEND_ESCAPED4(dest, cstr + 12);
                /* Five or more escapes add at least 15 bytes. */
                dense = dest - block >= 16 + 15;
            }
            cstr += 16;
        }
    }
#endif

    /* The rest 4 bytes at a time, then the last 0 to 3 bytes. */
    while (end - cstr >= 4) {
        APPEND_ESCAPED4(dest, cstr);
        cstr += 4;
    }
    while (cstr < end) {
        APPEND_ESCAPED(dest, *cstr, html_escape_table[*cstr].len);
        cstr++;
    }
    return dest;
}

static VALUE
optimized_escape_html(VALUE str)
{
    VALUE escaped;
    VALUE vbuf;
    char *buf, *dest;
    const unsigned char *start = (const unsigned char *)RSTRING_PTR(str);
    const unsigned char *end = start + RSTRING_LEN(str);
    const unsigned char *first;

    if (end - start < 16) {
        /*
         * Strings under 16 bytes: the old loop, into a stack buffer.  The
         * escapes are copied byte by byte because a memcpy into sbuf would
         * become a __memcpy_chk call with _FORTIFY_SOURCE=3.
         */
        char sbuf[15 * HTML_ESCAPE_MAX_LEN];
        const unsigned char *p;

        dest = sbuf;
        for (p = start; p < end; p++) {
            const unsigned char c = *p;
            const uint8_t len = html_escape_table[c].len;

            if (len) {
                const char *const e = html_escape_table[c].str;

                dest[0] = e[0];
                dest[1] = e[1];
                dest[2] = e[2];
                dest[3] = e[3];
                dest[4] = e[4];
                dest[5] = e[5];
                dest += len;
            }
            else {
                *dest++ = (char)c;
            }
        }
        if (dest - sbuf == end - start) {
            return rb_str_dup(str);
        }
        escaped = rb_str_new(sbuf, dest - sbuf);
        preserve_original_state(str, escaped);
        return escaped;
    }

    if (end - start < HTML_ESCAPE_ONE_PASS) {
        /*
         * Short strings: escape in one pass into a stack buffer, since
         * searching first would read them twice.
         */
        char sbuf[(HTML_ESCAPE_ONE_PASS - 1) * HTML_ESCAPE_MAX_LEN];

        dest = escape_html_into(sbuf, start, start, end);
        if (dest - sbuf == end - start) {
            return rb_str_dup(str);
        }
        escaped = rb_str_new(sbuf, dest - sbuf);
        preserve_original_state(str, escaped);
        return escaped;
    }

    first = find_first_escape(start, end);
    if (first == end) {
        return rb_str_dup(str);
    }

    buf = ALLOCV_N(char, vbuf, escaped_length(str));
    dest = escape_html_into(buf, start, first, end);
    escaped = rb_str_new(buf, dest - buf);
    preserve_original_state(str, escaped);
    ALLOCV_END(vbuf);
    return escaped;
}

static VALUE
optimized_unescape_html(VALUE str)
{
    enum {UNICODE_MAX = 0x10ffff};
    rb_encoding *enc = rb_enc_get(str);
    unsigned long charlimit = (strcasecmp(rb_enc_name(enc), "UTF-8") == 0 ? UNICODE_MAX :
                               strcasecmp(rb_enc_name(enc), "ISO-8859-1") == 0 ? 256 :
                               128);
    rb_len_t i, j, len, beg = 0;
    size_t clen, plen;
    int overflow;
    const char *cstr;
    char buf[6];
    VALUE dest = 0;

    len  = RSTRING_LEN(str);
    cstr = RSTRING_PTR(str);

    for (i = 0; i < len; i++) {
        rb_ulen_t cc;
        char c = cstr[i];
        if (c != '&') continue;
        plen = i - beg;
        if (++i >= len) break;
        c = (unsigned char)cstr[i];
        j = i;
#define MATCH(s) (len - i >= (int)rb_strlen_lit(s) && \
                  memcmp(&cstr[i], s, rb_strlen_lit(s)) == 0 && \
                  (i += rb_strlen_lit(s) - 1, 1))
        switch (c) {
          case 'a':
            ++i;
            if (MATCH("pos;")) {
                c = '\'';
            }
            else if (MATCH("mp;")) {
                c = '&';
            }
            else {
                i = j;
                continue;
            }
            break;
          case 'q':
            ++i;
            if (MATCH("uot;")) {
                c = '"';
            }
            else {
                i = j;
                continue;
            }
            break;
          case 'g':
            ++i;
            if (MATCH("t;")) {
                c = '>';
            }
            else {
                i = j;
                continue;
            }
            break;
          case 'l':
            ++i;
            if (MATCH("t;")) {
                c = '<';
            }
            else {
                i = j;
                continue;
            }
            break;
          case '#':
            if (len - ++i >= 2 && ISDIGIT(cstr[i])) {
                cc = ruby_scan_digits(&cstr[i], len-i, 10, &clen, &overflow);
            }
            else if ((cstr[i] == 'x' || cstr[i] == 'X') && len - ++i >= 2 && ISXDIGIT(cstr[i])) {
                cc = ruby_scan_digits(&cstr[i], len-i, 16, &clen, &overflow);
            }
            else {
                i = j;
                continue;
            }
            i += clen;
            if (overflow || cc >= charlimit || cstr[i] != ';') {
                i = j;
                continue;
            }
            if (!dest) {
                dest = rb_str_buf_new(len);
            }
            rb_str_cat(dest, cstr + beg, plen);
            if (charlimit > 256) {
                rb_str_cat(dest, buf, rb_enc_mbcput((OnigCodePoint)cc, buf, enc));
            }
            else {
                c = (unsigned char)cc;
                rb_str_cat(dest, &c, 1);
            }
            beg = i + 1;
            continue;
          default:
            --i;
            continue;
        }
        if (!dest) {
            dest = rb_str_buf_new(len);
        }
        rb_str_cat(dest, cstr + beg, plen);
        rb_str_cat(dest, &c, 1);
        beg = i + 1;
    }

    if (dest) {
        rb_str_cat(dest, cstr + beg, len - beg);
        preserve_original_state(str, dest);
        return dest;
    }
    else {
        return rb_str_dup(str);
    }
}

static unsigned char
url_unreserved_char(unsigned char c)
{
    switch (c) {
      case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
      case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j':
      case 'k': case 'l': case 'm': case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't':
      case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
      case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I': case 'J':
      case 'K': case 'L': case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T':
      case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
      case '-': case '.': case '_': case '~':
        return 1;
      default:
        break;
    }
    return 0;
}

static VALUE
optimized_escape(VALUE str, int plus_escape)
{
    rb_len_t i, len, beg = 0;
    VALUE dest = 0;
    const char *cstr;
    char buf[4] = {'%'};

    len  = RSTRING_LEN(str);
    cstr = RSTRING_PTR(str);

    for (i = 0; i < len; ++i) {
        const unsigned char c = (unsigned char)cstr[i];
        if (!url_unreserved_char(c)) {
            if (!dest) {
                dest = rb_str_buf_new(len);
            }

            rb_str_cat(dest, cstr + beg, i - beg);
            beg = i + 1;

            if (plus_escape && c == ' ') {
                rb_str_cat_cstr(dest, "+");
            }
            else {
                buf[1] = upper_hexdigits[(c >> 4) & 0xf];
                buf[2] = upper_hexdigits[c & 0xf];
                rb_str_cat(dest, buf, 3);
            }
        }
    }

    if (dest) {
        rb_str_cat(dest, cstr + beg, len - beg);
        preserve_original_state(str, dest);
        return dest;
    }
    else {
        return rb_str_dup(str);
    }
}

static VALUE
optimized_unescape(VALUE str, VALUE encoding, int unescape_plus)
{
    rb_len_t i, len, beg = 0;
    VALUE dest = 0;
    const char *cstr;
    rb_encoding *enc = rb_to_encoding(encoding);
    int cr, origenc, encidx = rb_enc_to_index(enc);

    len  = RSTRING_LEN(str);
    cstr = RSTRING_PTR(str);

    for (i = 0; i < len; ++i) {
        char buf[1];
        const char c = cstr[i];
        int clen = 0;
        if (c == '%') {
            if (i + 3 > len) break;
            if (!ISXDIGIT(cstr[i+1])) continue;
            if (!ISXDIGIT(cstr[i+2])) continue;
            buf[0] = ((char_to_number(cstr[i+1]) << 4)
                      | char_to_number(cstr[i+2]));
            clen = 2;
        }
        else if (unescape_plus && c == '+') {
            buf[0] = ' ';
        }
        else {
            continue;
        }

        if (!dest) {
            dest = rb_str_buf_new(len);
        }

        rb_str_cat(dest, cstr + beg, i - beg);
        i += clen;
        beg = i + 1;

        rb_str_cat(dest, buf, 1);
    }

    if (dest) {
        rb_str_cat(dest, cstr + beg, len - beg);
        preserve_original_state(str, dest);
        cr = ENC_CODERANGE_UNKNOWN;
    }
    else {
        dest = rb_str_dup(str);
        cr = ENC_CODERANGE(str);
    }
    origenc = rb_enc_get_index(str);
    if (origenc != encidx) {
        rb_enc_associate_index(dest, encidx);
        if (!ENC_CODERANGE_CLEAN_P(rb_enc_str_coderange(dest))) {
            rb_enc_associate_index(dest, origenc);
            if (cr != ENC_CODERANGE_UNKNOWN)
                ENC_CODERANGE_SET(dest, cr);
        }
    }
    return dest;
}

/*
 *  call-seq:
 *     CGI.escapeHTML(string) -> string
 *
 *  Returns HTML-escaped string.
 *
 */
static VALUE
cgiesc_escape_html(VALUE self, VALUE str)
{
    StringValue(str);

    if (rb_enc_str_asciicompat_p(str)) {
        return optimized_escape_html(str);
    }
    else {
        return rb_call_super(1, &str);
    }
}

/*
 *  call-seq:
 *     CGI.unescapeHTML(string) -> string
 *
 *  Returns HTML-unescaped string.
 *
 */
static VALUE
cgiesc_unescape_html(VALUE self, VALUE str)
{
    StringValue(str);

    if (rb_enc_str_asciicompat_p(str)) {
        return optimized_unescape_html(str);
    }
    else {
        return rb_call_super(1, &str);
    }
}

/*
 *  call-seq:
 *     CGI.escape(string) -> string
 *
 *  Returns URL-escaped string (+application/x-www-form-urlencoded+).
 *
 */
static VALUE
cgiesc_escape(VALUE self, VALUE str)
{
    StringValue(str);

    if (rb_enc_str_asciicompat_p(str)) {
        return optimized_escape(str, 1);
    }
    else {
        return rb_call_super(1, &str);
    }
}

static VALUE
accept_charset(int argc, VALUE *argv, VALUE self)
{
    if (argc > 0)
        return argv[0];
    return rb_cvar_get(CLASS_OF(self), id_accept_charset);
}

/*
 *  call-seq:
 *     CGI.unescape(string, encoding=@@accept_charset) -> string
 *
 *  Returns URL-unescaped string (+application/x-www-form-urlencoded+).
 *
 */
static VALUE
cgiesc_unescape(int argc, VALUE *argv, VALUE self)
{
    VALUE str = (rb_check_arity(argc, 1, 2), argv[0]);

    StringValue(str);

    if (rb_enc_str_asciicompat_p(str)) {
        VALUE enc = accept_charset(argc-1, argv+1, self);
        return optimized_unescape(str, enc, 1);
    }
    else {
        return rb_call_super(argc, argv);
    }
}

/*
 *  call-seq:
 *     CGI.escapeURIComponent(string) -> string
 *
 *  Returns URL-escaped string following RFC 3986.
 *
 */
static VALUE
cgiesc_escape_uri_component(VALUE self, VALUE str)
{
    StringValue(str);

    if (rb_enc_str_asciicompat_p(str)) {
        return optimized_escape(str, 0);
    }
    else {
        return rb_call_super(1, &str);
    }
}

/*
 *  call-seq:
 *     CGI.unescapeURIComponent(string, encoding=@@accept_charset) -> string
 *
 *  Returns URL-unescaped string following RFC 3986.
 *
 */
static VALUE
cgiesc_unescape_uri_component(int argc, VALUE *argv, VALUE self)
{
    VALUE str = (rb_check_arity(argc, 1, 2), argv[0]);

    StringValue(str);

    if (rb_enc_str_asciicompat_p(str)) {
        VALUE enc = accept_charset(argc-1, argv+1, self);
        return optimized_unescape(str, enc, 0);
    }
    else {
        return rb_call_super(argc, argv);
    }
}

void
Init_escape(void)
{
#ifdef HAVE_RB_EXT_RACTOR_SAFE
    rb_ext_ractor_safe(true);
#endif

    id_accept_charset = rb_intern_const("@@accept_charset");
    InitVM(escape);
}

void
InitVM_escape(void)
{
    rb_cCGI       = rb_define_class("CGI", rb_cObject);
    rb_mEscapeExt = rb_define_module_under(rb_cCGI, "EscapeExt");
    rb_mEscape    = rb_define_module_under(rb_cCGI, "Escape");
    rb_define_method(rb_mEscapeExt, "escapeHTML", cgiesc_escape_html, 1);
    rb_define_method(rb_mEscapeExt, "unescapeHTML", cgiesc_unescape_html, 1);
    rb_define_method(rb_mEscapeExt, "escapeURIComponent", cgiesc_escape_uri_component, 1);
    rb_define_alias(rb_mEscapeExt, "escape_uri_component", "escapeURIComponent");
    rb_define_method(rb_mEscapeExt, "unescapeURIComponent", cgiesc_unescape_uri_component, -1);
    rb_define_alias(rb_mEscapeExt, "unescape_uri_component", "unescapeURIComponent");
    rb_define_method(rb_mEscapeExt, "escape", cgiesc_escape, 1);
    rb_define_method(rb_mEscapeExt, "unescape", cgiesc_unescape, -1);
    rb_prepend_module(rb_mEscape, rb_mEscapeExt);
    rb_extend_object(rb_cCGI, rb_mEscapeExt);
}
