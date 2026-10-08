#ifndef NP_CFG_H
#define NP_CFG_H

#include "np_app.h"

/* Internal. Owner: src/np_cfg.c. Product API stays in np_host.h. */

/* 1 if the name is letters, digits, '-' or '_', and shorter than the profile-name limit. Empty, NULL, or anything else is 0. */
int prof_ok_name(const char *s);
/* Profile ini path for that name. A null name is written as default, and the name is not checked. */
void prof_file(const char *name, char *out, size_t n);
/* Writes view, channels, and NEG RAIL. with_map also writes the API block and the plus-site names. A failed open leaves the previous file; a successful open has already truncated it. */
int cfg_write_ex(const char *path, int with_map);
/* Writes a kit: filters, algo source, plus-site names, channels, and NEG RAIL, with no API block. A failed open leaves any previous kit file. */
int cfg_write_kit(const char *path);
/* Loads an ini into live settings, including plus sites when elec lines are present, and ignores pair_mode. NEG RAIL forces bias and CAR off after the read, and a missing file returns -1. */
int cfg_read(const char *path);
/* Pushes gains into the parser and, if the board is connected, channel and bias commands. Returns before those commands when nothing is connected. NEG RAIL still sends bias off. */
void prof_apply(void);
/* Rewrites the current profile without the electrode map. Returns immediately if the name is not legal, and does not write exg-c.ini. */
void prof_autosave(void);
/* Deletes the current profile file and clears the name. A bad name or a failed unlink leaves the name set. */
void prof_del(void);
/* Renames the profile file. The same name, or a name that is not legal, does not touch the file. */
void prof_rename(const char *to);
/* 1 and writes the integer if "key" appears as a JSON number. 0 if the key or the number is missing. First match only. */
int cfg_jint(const char *js, const char *key, int *out);
/* Copies the first line that is not blank and not a # or // comment, and cuts at the newline. A null source or a null out leaves an empty string, or returns without writing. */
void algo_first_line(const char *s, char *out, int n);
/* Source text for that cube bit. Out of range uses the live algo. A bit set to inherit (-1) does not climb to the cube algo; it gets the compare default. */
const char *made_src_or_default(int cube, int q);
/* 1 if channel ch (1..8) is already on another made cube. skip is the cube index to ignore, and the answer is 0 if ch is outside 1..8. */
int made_ch_used(int ch, int skip);
/* Path of the scratch kit file exg-c.kit. Does not create it. */
void kit_tmp(char *out, int n);
/* Rebuilds the desk-noise spectrum CLEAN uses and the calm plate from raw, turns the still-plate DC cut on if calm loaded, and rewrites takes and MATCH poses from raw without moving the electrode map. */
void data_recook(void);

#endif /* NP_CFG_H */
