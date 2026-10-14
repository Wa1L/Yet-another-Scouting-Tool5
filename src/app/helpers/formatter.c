// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "formatter.h"
#include "app/maths.h"
#include "app/types.h"
#include "core/logger.h"

#include <string.h>


extern GameContext gameContext;

/**
 * Add thousands separators to string-representations of integers.
 * Ensure the string is writable and there are enough spaces in
 * the string buffer to add them.
 */
void formatter_printNumber(char *outString) {
	if (outString == NULL) {
		return;
	}

	const uint64_t length = strlen(outString);
	const uint8_t minus = outString[0] == '-';
	if (length - minus < 4) {
		return;
	}

	const uint64_t commaCount = (length - 1 - minus) / 3;

	uint64_t src = length;
	uint64_t dest = length + commaCount;
	outString[dest--] = '\0';
	int64_t i = 0;
	while (src > minus) {
		outString[dest--] = outString[--src];

		if (src > minus && ++i == 3) {
			outString[dest--] = ',';
			i = 0;
		}
	}
}

#define POUND_SIGN_WIDTH 2
/**
 * Prefix a string with the GBP symbol (£)
 * Ensure the string is writable and there are
 * enough space sin the string buffer to add them.
 */
void formatter_printCurrency(char *outString) {
	if (outString == NULL) {
		return;
	}

	formatter_printNumber(outString);

	const uint64_t length = strlen(outString);
	memmove(outString + 2, outString, length + 1);

	// UTF-8 encoding of U+00A3 (£)
	outString[0] = (char)0xC2;
	outString[1] = (char)0xA3;
}

/**
 * Ratings and conditions are both quantised onto a 0-120 hue ramp before being colourised, so the
 * whole ramp is built once here instead of recomputing the hue maths for every row we render.
 *
 * On a light background the raw ramp is unreadable (pure hues sit at maximum brightness, so a top
 * rating renders as #00ff00 on white), hence the luminance cap. 0.1833 is the highest relative
 * luminance that still clears WCAG AA contrast of 4.5:1 against white. Dark mode keeps the raw
 * ramp, where full brightness is exactly what we want.
 */
#define QUALITY_RAMP_STEPS 121
#define QUALITY_RAMP_MAX_LUMINANCE_ON_LIGHT 0.1833f

static RGB qualityRamp[QUALITY_RAMP_STEPS];
static char qualityRampHex[QUALITY_RAMP_STEPS][8];

void formatter_init(void) {
	const bool capLuminanceForLightBackground = !gameContext.options.darkMode;

	for (uint16_t i = 0; i < QUALITY_RAMP_STEPS; ++i) {
		const RGB hue = hueToRgb((float)i);
		const RGB rgb = capLuminanceForLightBackground ? capLuminance(hue, QUALITY_RAMP_MAX_LUMINANCE_ON_LIGHT) : hue;

		qualityRamp[i] = rgb;
		snprintf(
			qualityRampHex[i],
			sizeof(qualityRampHex[i]),
			"#%02x%02x%02x",
			(uint8_t)roundf(rgb.r * 255.f),
			(uint8_t)roundf(rgb.g * 255.f),
			(uint8_t)roundf(rgb.b * 255.f)
		);
	}
}

/**
 * Returns the ramp colour for a value already quantised to 0-120.
 */
RGB formatter_qualityColour(const uint8_t value) {
	return qualityRamp[value];
}

/**
 * Returns a string containing your rating, colourised on a scale of 1-120
 * @param rating
 * @param outString - should be at least FORMATTER_RATING_SIZE bytes
 */
void formatter_formatRating(const float rating, char *outString) {
	const uint8_t value = (uint8_t)((rating < MAX_RATING_VALUE ? rating / MAX_RATING_VALUE : 1.f) * 120);
	snprintf(
		outString,
		FORMATTER_RATING_SIZE,
		"<span foreground=\"%s\">%.2f%%</span>",
		qualityRampHex[value],
		rating
	);
}

/**
 * Same value, no colour. Pango attributes beat CSS, so a coloured rating would ignore the selected
 * row's foreground and leave dark green sitting on the dark blue selection fill. Callers switch to
 * this while the row is selected and let the stylesheet pick the colour.
 * @param rating
 * @param outString - should be at least FORMATTER_RATING_PLAIN_SIZE bytes
 */
void formatter_formatRatingPlain(const float rating, char *outString) {
	snprintf(outString, FORMATTER_RATING_PLAIN_SIZE, "%.2f%%", rating);
}

/**
 * Colourises a plain 0-100 percentage. Unlike ratings these are whole numbers, so they print without
 * decimals.
 * @param percentage
 * @param inverted - for metrics like fatigue, where 0 is the healthy end of the scale
 * @param outString - should be at least FORMATTER_PERCENTAGE_SIZE bytes
 */
void formatter_formatPercentage(const float percentage, const bool inverted, char *outString) {
	const float clamped = percentage < 0.f ? 0.f : (percentage < 100.f ? percentage : 100.f);
	const uint8_t index = (uint8_t)(clamped * 1.2f);
	snprintf(
		outString,
		FORMATTER_PERCENTAGE_SIZE,
		"<span foreground=\"%s\">%.0f%%</span>",
		qualityRampHex[inverted ? 120 - index : index],
		clamped
	);
}

/**
 * Abbreviates an amount of money the way FM does: £950, £450K, £2.5M. Negative amounts keep their
 * sign, and `showPlus` adds one to positive amounts so the result reads as a change.
 * @param outString - should be at least FORMATTER_MONEY_SIZE bytes
 */
void formatter_formatMoneyShort(const int64_t value, const bool showPlus, char *outString) {
	const char *sign = value < 0 ? "-" : (showPlus && value > 0 ? "+" : "");
	const double magnitude = fabs((double)value);

	if (magnitude >= 1e9) {
		snprintf(outString, FORMATTER_MONEY_SIZE, "%s£%.2fB", sign, magnitude / 1e9);
	} else if (magnitude >= 1e6) {
		snprintf(outString, FORMATTER_MONEY_SIZE, "%s£%.1fM", sign, magnitude / 1e6);
	} else if (magnitude >= 1e3) {
		snprintf(outString, FORMATTER_MONEY_SIZE, "%s£%.0fK", sign, magnitude / 1e3);
	} else {
		snprintf(outString, FORMATTER_MONEY_SIZE, "%s£%.0f", sign, magnitude);
	}
}

/**
 * Wraps `text` in green markup when `delta` is an improvement and red when it is a decline, leaving
 * it plain when nothing changed. `text` must not contain markup characters.
 * @param outString - should be at least FORMATTER_DELTA_SIZE bytes
 */
void formatter_formatDelta(const double delta, const bool higherIsBetter, const char *text, char *outString) {
	if (delta == 0.0) {
		snprintf(outString, FORMATTER_DELTA_SIZE, "%s", text);
		return;
	}

	const bool isImprovement = (delta > 0.0) == higherIsBetter;
	snprintf(
		outString,
		FORMATTER_DELTA_SIZE,
		"<span foreground=\"%s\">%s</span>",
		qualityRampHex[isImprovement ? 120 : 0],
		text
	);
}
