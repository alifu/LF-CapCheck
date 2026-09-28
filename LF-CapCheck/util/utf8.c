#include "util/utf8.h"

#include <stddef.h>
#include <stdint.h>

#define CONTINUATION_MASK 0xC0
#define CONTINUATION_TAG 0x80
#define CONTINUATION_BITS 6
#define CONTINUATION_PAYLOAD 0x3F
#define MAX_CODE_POINT 0x10FFFF
#define SURROGATE_FIRST 0xD800
#define SURROGATE_LAST 0xDFFF
#define DELETE_CHARACTER 0x7F
#define C1_LAST 0x9F
#define FIRST_PRINTABLE 0x20

/* How many bytes a sequence starting with `lead` has, its payload bits and its smallest legal value. */
typedef struct {
    size_t length;
    uint32_t lead_payload;
    uint32_t smallest;
} sequence_shape_t;

static sequence_shape_t shape_for(unsigned char lead)
{
    if (lead < 0x80) {
        return (sequence_shape_t){1, lead, 0};
    }
    if ((lead & 0xE0) == 0xC0) {
        return (sequence_shape_t){2, lead & 0x1Fu, 0x80};
    }
    if ((lead & 0xF0) == 0xE0) {
        return (sequence_shape_t){3, lead & 0x0Fu, 0x800};
    }
    if ((lead & 0xF8) == 0xF0) {
        return (sequence_shape_t){4, lead & 0x07u, 0x10000};
    }
    return (sequence_shape_t){0, 0, 0}; /* continuation byte, or 0xF8..0xFF: never a lead byte */
}

/* Decodes one code point. Returns the bytes used, or 0 if the sequence is malformed. */
static size_t decode_code_point(const unsigned char *text, uint32_t *code_point)
{
    sequence_shape_t shape = shape_for(text[0]);
    uint32_t value = shape.lead_payload;

    if (shape.length == 0) {
        return 0;
    }
    for (size_t i = 1; i < shape.length; i++) {
        if ((text[i] & CONTINUATION_MASK) != CONTINUATION_TAG) { /* also catches the terminator */
            return 0;
        }
        value = (value << CONTINUATION_BITS) | (text[i] & CONTINUATION_PAYLOAD);
    }
    if (value < shape.smallest || value > MAX_CODE_POINT ||
        (value >= SURROGATE_FIRST && value <= SURROGATE_LAST)) {
        return 0; /* overlong, out of range or a surrogate */
    }
    *code_point = value;
    return shape.length;
}

static bool is_control(uint32_t code_point)
{
    return code_point < FIRST_PRINTABLE || (code_point >= DELETE_CHARACTER && code_point <= C1_LAST);
}

bool utf8_is_plain_text(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;

    if (text == NULL) {
        return false;
    }
    while (*cursor != '\0') {
        uint32_t code_point = 0;
        size_t used = decode_code_point(cursor, &code_point);

        if (used == 0 || is_control(code_point)) {
            return false;
        }
        cursor += used;
    }
    return true;
}
