/**
 * \file            lwjson.c
 * \brief           Lightweight JSON format parser
 */

/*
 * Copyright (c) 2020 Tilen MAJERLE
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * This file is part of LwJSON - Lightweight JSON format parser.
 *
 * Author:          Tilen MAJERLE <tilen@majerle.eu>
 * Version:         $_version_$
 */
#include <string.h>
#include "lwjson/lwjson.h"

/**
 * \brief           Allocate new token for JSON block
 * \param[in]       lw: LwJSON instance
 * \return          Pointer to new token
 */
static lwjson_token_t*
prv_alloc_token(lwjson_t* lw) {
    if (lw->next_free_token_pos < lw->tokens_len) {
        memset(&lw->tokens[lw->next_free_token_pos], 0x00, sizeof(*lw->tokens));
        return &lw->tokens[lw->next_free_token_pos++];
    }
    return NULL;
}

/**
 * \brief           Skip all characters that are considered *blank* as per RFC4627
 * \param[in,out]   p: Pointer to text that is modified on success
 * \return          \ref lwjsonOK on success, member of \ref lwjsonr_t otherwise
 */
static lwjsonr_t
prv_skip_blank(const char** p) {
    const char* s = *p;
    while (s != NULL && *s != '\0') {
        if (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' || *s == '\f') {
            ++s;
            continue;
        }
        *p = s;
        return lwjsonOK;
    }
    if (s != NULL && *s == '\0') {
        return lwjsonOK;
    }
    return lwjsonERR;
}

/**
 * \brief           Parse JSON string that must start end end with double quotes `"` character
 * It just parses length of characters and does not perform any decode operation
 * \param[in,out]   p: Pointer to text that is modified on success
 * \param[out]      pout: Pointer to pointer to string that is set where string starts
 * \param[out]      poutlen: Length of string in units of characters is stored here
 * \return          \ref lwjsonOK on success, member of \ref lwjsonr_t otherwise
 */
static lwjsonr_t
prv_parse_string(const char** p, const char** pout, size_t* poutlen) {
    const char* s;
    lwjsonr_t res;
    size_t len = 0;

    if ((res = prv_skip_blank(p)) != lwjsonOK) {
        return res;
    }
    s = *p;
    if (*s++ != '"') {
        return lwjsonERRJSON;
    }
    *pout = s;
    /* Parse string but take care of escape characters */
    for (char prev_ch = '\0';; ++s, ++len) {
        if (s == NULL || *s == '\0') {
            return lwjsonERRJSON;
        }
        /* Check end of string */
        if (*s == '"') {
            if (prev_ch != '\\') {
                ++s;
                break;
            }
        }
        prev_ch = *s;
    }
    *poutlen = len;
    if (*s == '"') {
        ++s;
    }
    if ((res = prv_skip_blank(&s)) != lwjsonOK) {
        return res;
    }
    *p = s;
    return res;
}

/**
 * \brief           Parse property name that must comply with JSON string format as in RFC4627
 * Property string must be followed by colon character ":"
 * \param[in,out]   p: Pointer to text that is modified on success
 * \param[out]      t: Token instance to write property name to
 * \return          \ref lwjsonOK on success, member of \ref lwjsonr_t otherwise
 */
static lwjsonr_t
prv_parse_property_name(const char** p, lwjson_token_t* t) {
    const char* s = *p;
    lwjsonr_t res;

    if ((res = prv_parse_string(p, &t->token_name, &t->token_name_len)) != lwjsonOK) {
        return res;
    }
    s = *p;
    if (*s != ':') {
        return lwjsonERRJSON;
    }
    ++s;
    prv_skip_blank(&s);
    *p = s;
    return lwjsonOK;
}

/**
 * \brief           Parse number as described in RFC4627
 * \param[in,out]   p: Pointer to text that is modified on success
 * \param[out]      tout: Pointer to output number format
 * \param[out]      fout: Pointer to output real-type variable. Used if type is REAL.
 * \param[out]      iout: Pointer to output int-type variable. Used if type is INT.
 * \return          \ref lwjsonOK on success, member of \ref lwjsonr_t otherwise
 */
static lwjsonr_t
prv_parse_number(const char** p, lwjson_type_t* tout, lwjson_real_t* fout, lwjson_int_t* iout) {
    const char* s = *p;
    lwjsonr_t res;
    uint8_t is_minus;
    lwjson_real_t num;
    lwjson_type_t type = LWJSON_TYPE_NUM_INT;

    if ((res = prv_skip_blank(p)) != lwjsonOK) {
        return res;
    }
    s = *p;
    if (*s == '\0') {
        return lwjsonERRJSON;
    }
    is_minus = *s == '-' ? (++s, 1) : 0;
    if (*s == '\0'                              /* Invalid string */
        || *s < '0' || *s > '9'                 /* Character outside number range */
        || (*s == '0' && (*(s + 1) < '0' && *(s + 1) > '9'))) { /* Number starts with 0 but not followed by dot */
        return lwjsonERRJSON;
    }
    /* Parse number */
    for (num = 0; *s >= '0' && *s <= '9'; ++s) {
        num = num * 10 + (*s - '0');
    }
    if (s != NULL && *s == '.') {               /* Number has exponent */
        lwjson_real_t exp, dec_num;

        type = LWJSON_TYPE_NUM_REAL;            /* Format is real */
        ++s;                                    /* Ignore comma character */
        if (*s < '0' || *s > '9') {             /* Must be followed by number characters */
            return lwjsonERRJSON;
        }
        /* Get number after decimal point */
        for (exp = 1, dec_num = 0; *s >= '0' && *s <= '9'; ++s, exp *= 10) {
            dec_num = dec_num * 10 + (*s - '0');
        }
        num += dec_num / exp;                   /* Add decimal part to number */
    }
    if (s != NULL && (*s == 'e' || *s == 'E')) {/* Engineering mode */
        uint8_t is_minus_exp;
        int exp_cnt;

        type = LWJSON_TYPE_NUM_REAL;            /* Format is real */
        ++s;                                    /* Ignore enginnering sing part */
        is_minus_exp = *s == '-' ? (++s, 1) : 0;/* Check if negative */
        if (*s == '+') {                        /* Optional '+' is possible too */
            ++s;
        }
        if (*s < '0' || *s > '9') {             /* Must be followed by number characters */
            return lwjsonERRJSON;
        }

        /* Parse exponent number */
        for (exp_cnt = 0; *s >= '0' && *s <= '9'; ++s) {
            exp_cnt = exp_cnt * 10 + (*s - '0');
        }
        /* Calculate new value for exponent 10^exponent */
        if (is_minus_exp) {
            for (; exp_cnt > 0; num /= 10, --exp_cnt) {}
        } else {
            for (; exp_cnt > 0; num *= 10, --exp_cnt) {}
        }
    }
	if (is_minus) {
        num = -num;
	}
    *p = s;

    /* Write output values */
    if (tout != NULL) {
        *tout = type;
    }
    if (type == LWJSON_TYPE_NUM_INT) {
        *iout = (lwjson_int_t)num;
    } else {
        *fout = num;
    }
	return lwjsonOK;
}

/**
 * \brief           Create path segment from input path for search operation
 * \param[in,out]   p: Pointer to pointer to input path. Pointer is modified
 * \param[out]      opath: Pointer to pointer to write path segment
 * \param[out]      olen: Pointer to variable to write length of segment
 * \param[out]      is_last: Pointer to write if this is last segment
 * \return          `1` on success, `0` otherwise
 */
static uint8_t
prv_create_path_segment(const char** p, const char** opath, size_t* olen, uint8_t* is_last) {
    const char* s = *p;

    *is_last = 0;
    *opath = NULL;
    *olen = 0;

    /* Check input path */
    if (s == NULL || *s == '\0') {
        return 0;
    }

    /* 
     * Path must be one of:
     * - literal text
     * - "#" followed by dot "."
     */
    if (*s == '#') {
        if (*(s + 1) != '.') {
            return 0;
        }
        *opath = s;
        *olen = 1;
        *p = s + *olen + 1;
    } else {
        *opath = s;
        for (*olen = 0; *s != '\0' && *s != '.'; ++(*olen), ++s) {}
        *p = s + 1;
    }
    if (*s == '\0') {
        *is_last = 1;
    }
    return 1;
}

/**
 * \brief           Input recursive function for find operation
 * \param[in]       parent: Parent token of type \ref LWJSON_TYPE_ARRAY or LWJSON_TYPE_OBJECT
 * \param[in]       path: Path to search for starting this token further
 * \return          Found token on success, `NULL` otherwise
 */
static const lwjson_token_t*
prv_find(const lwjson_token_t* parent, const char* path) {
    lwjson_token_t* token = NULL;
    const char* segment;
    size_t segment_len;
    uint8_t is_last, result;

    /* Get path segments */
    if ((result = prv_create_path_segment(&path, &segment, &segment_len, &is_last)) != 0) {
        /* Check if detected an array request */
        if (*segment == '#' && segment_len == 1) {
            if (parent->type != LWJSON_TYPE_ARRAY) {
                return NULL;
            }
            for (const lwjson_token_t* tmp_t, *t = parent->u.first_child; t != NULL; t = t->next) {
                if ((tmp_t = prv_find(t, path)) != NULL) {
                    return tmp_t;
                }
            }
        } else {
            if (parent->type != LWJSON_TYPE_OBJECT) {
                return NULL;
            }
            for (const lwjson_token_t* t = parent->u.first_child; t != NULL; t = t->next) {
                if (t->token_name_len == segment_len && !strncmp(t->token_name, segment, segment_len)) {
                    const lwjson_token_t* tmp_t;
                    if (is_last) {
                        return t;
                    }
                    if ((tmp_t = prv_find(t, path)) != NULL) {
                        return tmp_t;
                    }
                }
            }
        }
    }
    return NULL;
}

/**
 * \brief           Setup LwJSON instance for parsing JSON strings
 * \param[in,out]   lw: LwJSON instance
 * \param[in]       tokens: Pointer to array of tokens used for parsing
 * \param[in]       tokens_len: Number of tokens
 * \return          \ref lwjsonOK on success, member of \ref lwjsonr_t otherwise
 */
lwjsonr_t
lwjson_init(lwjson_t* lw, lwjson_token_t* tokens, size_t tokens_len) {
    memset(lw, 0x00, sizeof(*lw));
    memset(tokens, 0x00, sizeof(*tokens) * tokens_len);
    lw->tokens = tokens;
    lw->tokens_len = tokens_len;
    lw->first_token.type = LWJSON_TYPE_OBJECT;
    return lwjsonOK;
}

/**
 * \brief           Parse input JSON format
 * JSON format must be complete and must comply with RFC4627
 * \param[in,out]   lw: LwJSON instance
 * \param[in]       json_str: JSON string to parse
 * \return          \ref lwjsonOK on success, member of \ref lwjsonr_t otherwise
 */
lwjsonr_t
lwjson_parse(lwjson_t* lw, const char* json_str) {
    lwjsonr_t res = lwjsonOK;
    const char* p = json_str;
    lwjson_token_t* t, *to = &lw->first_token;
    uint8_t first_check = 1;

    /* values from very beginning */
    lw->flags.parsed = 0;
    lw->next_free_token_pos = 0;
    memset(to, 0x00, sizeof(*to));

    /* Check input data first */
    if (p == NULL || *p == '\0') {
        return lwjsonERRJSON;
    }

    /* Process all characters */
    while (p != NULL && *p != '\0') {
        /* Filter out blanks */
        if ((res = prv_skip_blank(&p)) != lwjsonOK) {
            goto ret;
        }
        if (first_check) {
            first_check = 0;
            if (*p == '{') {
                to->type = LWJSON_TYPE_OBJECT;
            } else if (*p == '[') {
                to->type = LWJSON_TYPE_ARRAY;
            } else {
                res = lwjsonERRMEM;
                goto ret;
            }
            ++p;
            continue;
        }
        if (*p == ',') {
            ++p;
            continue;
        }

        /* Check if end of object or array*/
        if (*p == (to->type == LWJSON_TYPE_OBJECT ? '}' : ']')) {
            lwjson_token_t* parent = to->next;
            to->next = NULL;
            to = parent;
            ++p;

            /* End of string, check if properly terminated */
            if (to == NULL) {
                prv_skip_blank(&p);
                res = (p == NULL || *p == '\0') ? lwjsonOK : lwjsonERR;
                goto ret;
            }
            continue;
        }

        /* Allocate new token */
        t = prv_alloc_token(lw);
        if (t == NULL) {
            res = lwjsonERRMEM;
            goto ret;
        }

        /* If object type is not array, first thing is property that starts with quotes */
        if (to->type != LWJSON_TYPE_ARRAY) {
            if (*p != '"') {
                res = lwjsonERRJSON;
                goto ret;
            }
            if ((res = prv_parse_property_name(&p, t)) != lwjsonOK) {
                goto ret;
            }
        }
        
        /* Add element to linked list */
        if (to->u.first_child == NULL) {
            to->u.first_child = t;
        } else {
            lwjson_token_t* c;
            for (c = to->u.first_child; c->next != NULL; c = c->next) {}
            c->next = t;
        }

        /* Check next character to process */
        switch (*p) {
            case '{':
            case '[':
                t->type = *p == '{' ? LWJSON_TYPE_OBJECT : LWJSON_TYPE_ARRAY;
                t->next = to;           /* Temporary saved as parent object */
                to = t;
                ++p;
                break;
            case '"':
                if ((res = prv_parse_string(&p, &t->u.str.token_value, &t->u.str.token_value_len)) == lwjsonOK) {
                    t->type = LWJSON_TYPE_STRING;
                } else {
                    goto ret;
                }
                break;
            case 't':
                /* RFC4627 is lower-case only */
                if (strncmp(p, "true", 4) == 0) {
                    t->type = LWJSON_TYPE_TRUE;
                    p += 4;
                } else {
                    res = lwjsonERRJSON;
                    goto ret;
                }
                break;
            case 'f':
                /* RFC4627 is lower-case only */
                if (strncmp(p, "false", 5) == 0) {
                    t->type = LWJSON_TYPE_FALSE;
                    p += 5;
                } else {
                    res = lwjsonERRJSON;
                    goto ret;
                }
                break;
            case 'n':
                /* RFC4627 is lower-case only */
                if (strncmp(p, "null", 4) == 0) {
                    t->type = LWJSON_TYPE_NULL;
                    p += 4;
                } else {
                    res = lwjsonERRJSON;
                    goto ret;
                }
                break;
            default:
                if (*p == '-' || (*p >= '0' && *p <= '9')) {
                    if (prv_parse_number(&p, &t->type, &t->u.num_real, &t->u.num_int) != lwjsonOK) {
                        res = lwjsonERRJSON;
                        goto ret;
                    }
                } else {
                    res = lwjsonERRJSON;
                    goto ret;
                }
                break;
        }

        /* Below code is used to check characters after valid tokens */
        if (t->type == LWJSON_TYPE_ARRAY || t->type == LWJSON_TYPE_OBJECT) {
            continue;
        }

        /*
         * Check what are values after the token value
         *
         * As per RFC4627, every token value may have one or more
         *  blank characters, followed by one of below options:
         *  - Comma separator for next token
         *  - End of array indication
         *  - End of object indication
         */
        if ((res = prv_skip_blank(&p)) != lwjsonOK) {
            goto ret;
        }
        /* Check if valid string is availabe after */
        if (p == NULL || *p == '\0' || (*p != ',' && *p != ']' && *p != '}')) {
            res = lwjsonERRJSON;
            goto ret;
        } else if (*p == ',') {                 /* Check to advance to next token immediatey */
            ++p;
        }
    }
    if (to != NULL) {
        to->token_name = NULL;
        to->token_name_len = 0;
    }
ret:
    if (res == lwjsonOK) {
        lw->flags.parsed = 1;
    }
    return res;
}

/**
 * \brief           Reset token instances and prepare for new parsing
 * \param[in,out]   lw: LwJSON instance
 * \return          \ref lwjsonOK on success, member of \ref lwjsonr_t otherwise
 */
lwjsonr_t
lwjson_reset(lwjson_t* lw) {
    memset(lw->tokens, 0x00, sizeof(*lw->tokens) * lw->tokens_len);
    return lwjsonOK;
}

/**
 * \brief           Find first match in the given path for JSON entry
 * JSON must be valid and parsed with \ref lwjson_parse function
 * \param[in]       lw: JSON instance with parsed JSON string
 * \param[in]       path: Path with dot-separated entries to search for the JSON key to return
 * \return          Pointer to found token on success, `NULL` if token cannot be found
 */
const lwjson_token_t*
lwjson_find(lwjson_t* lw, const char* path) {
    if (lw == NULL || !lw->flags.parsed || path == NULL) {
        return NULL;
    }
    return prv_find(lwjson_get_first_token(lw), path);
}
