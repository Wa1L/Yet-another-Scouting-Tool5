// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "text.h"

#include <stdint.h>
#include <string.h>


/**
 * Base letters for U+00C0 to U+017F (Latin-1 Supplement and Latin Extended-A), which between them
 * cover the accented characters in nearly every name in the database. Ligatures fold to their first
 * letter and the two arithmetic signs to a space.
 */
static const char latinFolds[] =
	"aaaaaaaceeeeiiii" "dnooooo ouuuuyts" "aaaaaaaceeeeiiii" "dnooooo ouuuuyty"
	"aaaaaa" "cccccccc" "dddd" "eeeeeeeeee" "gggggggg" "hhhh" "iiiiiiiiii" "ii" "jj" "kkk"
	"llllllllll" "nnnnnnnnn" "oooooooo" "rrrrrr" "ssssssss" "tttttt" "uuuuuuuuuuuu" "ww" "yyy"
	"zzzzzz" "s";

_Static_assert(sizeof(latinFolds) - 1 == 0x180 - 0xC0, "One fold per code point from U+00C0 to U+017F");

/**
 * Lower-cases and strips accents without allocating, so a search can fold every name in the cache
 * on each keystroke. Anything outside the folded range is copied through byte for byte, which keeps
 * other scripts matching themselves exactly.
 */
void text_fold(const char *text, char *outText, const size_t size) {
	if (size == 0) {
		return;
	}

	const unsigned char *in = (const unsigned char*)text;
	size_t length = 0;
	while (*in != '\0' && length + 1 < size) {
		const unsigned char c = *in;
		if (c >= 'A' && c <= 'Z') {
			outText[length++] = (char)(c + ('a' - 'A'));
			++in;
		} else if (c >= 0xC3 && c <= 0xC5 && (in[1] & 0xC0) == 0x80) {
			const uint32_t codePoint = (uint32_t)(c & 0x1F) << 6 | (uint32_t)(in[1] & 0x3F);
			if (codePoint >= 0xC0) {
				outText[length++] = latinFolds[codePoint - 0xC0];
			} else {
				// U+0080 to U+00BF are symbols, not letters; keep them intact if they fit
				if (length + 2 >= size) {
					break;
				}
				outText[length++] = (char)in[0];
				outText[length++] = (char)in[1];
			}
			in += 2;
		} else {
			outText[length++] = (char)c;
			++in;
		}
	}

	outText[length] = '\0';
}

bool text_containsFolded(const char *text, const char *foldedNeedle) {
	char folded[128];
	text_fold(text, folded, sizeof(folded));
	return strstr(folded, foldedNeedle) != NULL;
}
