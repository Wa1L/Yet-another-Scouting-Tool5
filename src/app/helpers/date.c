// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "date.h"

#include <stdio.h>


extern GameContext gameContext;

const char *date_getOrdinal(uint16_t day) {
	if (day >= 11 && day <= 13) {
		return "th";
	}

	switch (day % 10) {
		case 1: return "st";
		case 2: return "nd";
		case 3: return "rd";
		default: return "th";
	}
}

#define DAYS_IN_JANUARY 31
#define DAYS_IN_FEBRUARY 28
#define DAYS_IN_MARCH 31
#define DAYS_IN_APRIL 30
#define DAYS_IN_MAY 31
#define DAYS_IN_JUNE 30
#define DAYS_IN_JULY 31
#define DAYS_IN_AUGUST 31
#define DAYS_IN_SEPTEMBER 30
#define DAYS_IN_OCTOBER 31
#define DAYS_IN_NOVEMBER 30
#define DAYS_IN_DECEMBER 31

#define DAYS_BEFORE_FEBRUARY DAYS_IN_JANUARY
#define DAYS_BEFORE_MARCH (DAYS_IN_FEBRUARY + DAYS_BEFORE_FEBRUARY)
#define DAYS_BEFORE_APRIL (DAYS_IN_MARCH + DAYS_BEFORE_MARCH)
#define DAYS_BEFORE_MAY (DAYS_IN_APRIL + DAYS_BEFORE_APRIL)
#define DAYS_BEFORE_JUNE (DAYS_IN_MAY + DAYS_BEFORE_MAY)
#define DAYS_BEFORE_JULY (DAYS_IN_JUNE + DAYS_BEFORE_JUNE)
#define DAYS_BEFORE_AUGUST (DAYS_IN_JULY + DAYS_BEFORE_JULY)
#define DAYS_BEFORE_SEPTEMBER (DAYS_IN_AUGUST + DAYS_BEFORE_AUGUST)
#define DAYS_BEFORE_OCTOBER (DAYS_IN_SEPTEMBER + DAYS_BEFORE_SEPTEMBER)
#define DAYS_BEFORE_NOVEMBER (DAYS_IN_OCTOBER + DAYS_BEFORE_OCTOBER)
#define DAYS_BEFORE_DECEMBER (DAYS_IN_NOVEMBER + DAYS_BEFORE_NOVEMBER)

DayMonthYearTime date_prettify(const DateTime dateTime) {
	static const struct {
		uint16_t threshold;
		uint16_t offset;
		const char *name;
	} months[12] = {
		{DAYS_BEFORE_FEBRUARY, 0, "January"},
		{DAYS_BEFORE_MARCH, DAYS_BEFORE_FEBRUARY, "February"},
		{DAYS_BEFORE_APRIL, DAYS_BEFORE_MARCH, "March"},
		{DAYS_BEFORE_MAY, DAYS_BEFORE_APRIL, "April"},
		{DAYS_BEFORE_JUNE, DAYS_BEFORE_MAY, "May"},
		{DAYS_BEFORE_JULY, DAYS_BEFORE_JUNE, "June"},
		{DAYS_BEFORE_AUGUST, DAYS_BEFORE_JULY, "July"},
		{DAYS_BEFORE_SEPTEMBER, DAYS_BEFORE_AUGUST, "August"},
		{DAYS_BEFORE_OCTOBER, DAYS_BEFORE_SEPTEMBER, "September"},
		{DAYS_BEFORE_NOVEMBER, DAYS_BEFORE_OCTOBER, "October"},
		{DAYS_BEFORE_DECEMBER, DAYS_BEFORE_NOVEMBER, "November"},
		{UINT16_MAX, DAYS_BEFORE_DECEMBER, "December"},
	};

	DayMonthYearTime dayMonthYearTime = {.year = dateTime.year, .time = dateTime.time};
	for (uint8_t i = 0; i < 12; ++i) {
		if (dateTime.days <= months[i].threshold) {
			dayMonthYearTime.day = dateTime.days - months[i].offset;
			strncpy(dayMonthYearTime.month, months[i].name, MONTH_NAME_LENGTH - 1);
			dayMonthYearTime.month[MONTH_NAME_LENGTH - 1] = '\0';
			break;
		}
	}

	if (dateTime.time == 0) {
		strncpy(dayMonthYearTime.timeString, "00:00", 5);
	} else {
		static char m[4][3] = {"00", "15", "30", "45"};
		const uint8_t t = dateTime.time - 1;
		const uint8_t h = 6 + (t >> 2);
		snprintf(dayMonthYearTime.timeString, 6, "%d:%s", h, m[t & 0x03]);
	}

	return dayMonthYearTime;
}

/**
 * Days from the current in-game date to `date`, negative once it has passed. Both dates are on the
 * game's 365-day calendar, so this is exact to the day rather than an approximation.
 */
int32_t date_daysUntil(const Date date) {
	return ((int32_t)date.year - (int32_t)gameContext.currentDate.year) * 365
		+ (int32_t)date.days - (int32_t)gameContext.currentDate.days;
}

// Whole months until `date`, using an average month length; negative once it has passed.
int32_t date_monthsUntil(const Date date) {
	const int32_t days = date_daysUntil(date);
	return (int32_t)((float)days / (365.f / 12.f));
}

/** Writes e.g. "30 Jun 2027". */
void date_format(const Date date, char *outString, const size_t size) {
	const DayMonthYearTime d = date_prettify((DateTime){.days = date.days, .year = date.year});
	snprintf(outString, size, "%d %.3s %d", d.day, d.month, d.year);
}
