/*-
 * Copyright (c) 2016 Iain Hibbert
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * bcmfw-install [source-dir]
 *
 * search for the *.inf file [in the directory given], parse
 * it to discover which devices have PatchRAM files, copy them to
 * the libdata directory for each device.
 */

#include <sys/types.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

struct line {
	char *		text;
	struct line *	next;
};

struct section {
	char *		name;
	struct line *	lines;
	struct section *next;
};

struct model {
	unsigned int	vid;
	unsigned int	pid;
	char *		file;
	struct model *	next;
};

static const char	fwdir[] = BCMFW_DIR;

static struct section *	sections;
static struct model *	models;
static int		nmodels;
static int		nfiles;

static char *		DriverDate;
static char *		DriverVersion;
static unsigned int	VendorID;
static unsigned int	ProductID;

static void
line_add(struct section *s, uint8_t *ptr, size_t len)
{
	struct line *l;

	l = emalloc(sizeof(struct line));
	l->text = estrndup((char *)ptr, len);
	l->next = s->lines;
	s->lines = l;
}

static struct section *
section_add(uint8_t *ptr, size_t len)
{
	struct section *s;

	for (s = sections; s != NULL; s = s->next) {
		if (strncasecmp(s->name, (char *)ptr, len) == 0
		    && s->name[len] == '\0')
			return s;
	}

	s = emalloc(sizeof(struct section));
	s->name = estrndup((char *)ptr, len);
	s->lines = NULL;
	s->next = sections;
	sections = s;

	return s;
}

static void
model_add(unsigned int vid, unsigned int pid, char *file)
{
	struct model *m, **p;

	for (p = &models, m = *p; m != NULL; p = &m->next, m = *p) {
		if (vid == m->vid && pid == m->pid)
			return;	/* ignore dupes */

		if (vid == m->vid && pid < m->pid)
			break;

		if (vid < m->vid)
			break;
	}

	if (file[0] == '"') {
		*strrchr(file, '"') = '\0';
		file++;
	}

	m = emalloc(sizeof(struct model));
	m->vid = vid;
	m->pid = pid;
	m->file = estrdup(file);

	m->next = *p;
	*p = m;

	nmodels++;
}

/*
 * Read the .INF file. The rules we follow here are
 *	a. max line length is 4096 bytes
 *	b. <esc><esc> is not <esc>
 *	c. <esc><end-of-line> is skipped
 *	d. leading <space> is skipped
 *	e. trailing <space> is skipped
 *	f. <quote> % <quote> is not <stringkey>
 *	g. <quote> ; <quote> is not <semicolon>
 *	h. <stringkey> " <stringkey> is not <quote>
 *	i. <stringkey> ; <stringkey> is not <semicolon>
 *	j. all chars after <semicolon> are skipped
 *	k. empty lines are skipped
 */
static void
read_inf(const char *name)
{
	uint8_t	buf[4096];
	struct section *section;
	FILE *	f;
	size_t	len, space, lineno;
	bool	esc, quote, comment, stringkey;
	bool	leading;
	int	ch;

	f = efopen(name, "r");
	lineno = 0;
	section = NULL;

start:
	len = 0;
	space = 0;

	esc = false;
	quote = false;
	comment = false;
	stringkey = false;

	leading = true;

	lineno++;

	while (len < __arraycount(buf)) {
		ch = fgetc(f);
		if (ch == '\n' || ch == EOF) {
			if (esc) {
				if (!comment)
					len--;	/* drop <backslash> */

				if (ch == '\n') {
					esc = false;
					lineno++;
					continue;
				}
			}
			if (quote)
				warnx("unterminated quote on line #%zu", lineno);
			if (stringkey)
				warnx("unterminated string key on line #%zu", lineno);
			if (space)
				len = space;	/* drop trailing spaces */
			if (ch == EOF) {
				if (len == 0) {
					if (ferror(f))
						warn("Read error on %s", name);

					fclose(f);
					return;
				}

				warnx("missing newline at end of file");
			}
			if (len == 0)
				goto start;	/* ignore empty lines */
			if (buf[0] == '[') {
				if (buf[len - 1] == ']')
					section = section_add(buf + 1, len - 2);
				else {
					warnx("malformed section header on line #%zu", lineno);
					section = NULL;
				}
			} else if (section != NULL) /* ignore lines with no section */
				line_add(section, buf, len);

			goto start;
		}
		if (ch == ';' && !quote && !stringkey)
			comment = true;
		if (ch == '\\')
			esc = !esc;
		else
			esc = false;

		if (ch == '\r' || comment)
			continue;

		if (ch == '%' && !quote)
			stringkey = !stringkey;
		if (ch == '"' && !stringkey)
			quote = !quote;
		if ((ch == '=' || ch == ',') && !quote && !stringkey) {
			if (space) {
				len = space;	/* drop spaces */
				space = 0;
			}
			leading = true;
		} else if ((ch == ' ' || ch == '\t') && !quote && !stringkey) {
			if (leading)	/* skip leading spaces */
				continue;
			if (space == 0)	/* mark start of space */
				space = len;
		} else {
			space = 0;
			leading = false;
		}

		buf[len++] = (uint8_t)ch;
	}

	err(EXIT_FAILURE, "line #%zu too long", lineno);
}

static bool
section_foreach(const char *name, void (*func)(char **, size_t))
{
	struct section *s;
	struct line *l;
	char *av[10], *p, *t, *text, sep;
	bool quote, stringkey;
	size_t n;

	/*
	 * lookup section
	 */
	for (s = sections;; s = s->next) {
		if (s == NULL)
			return false;

		if (strcasecmp(s->name, name) == 0)
			break;
	}

	/*
	 * parse each line into an arg array
	 *
	 *	<0> = <1> [, <2> ... ]
	 *
	 * and call the function.
	 */
	for (l = s->lines; l; l = l->next) {
		n = 0;
		sep = '=';
		quote = false;
		stringkey = false;

		t = text = estrdup(l->text);
		for (p = t; n < __arraycount(av) - 1; p++) {
			if (*p == '\0') {
				if (sep == ',')
					break;

				av[n++] = p;	/* empty key */
				p = t;		/* restart */
				sep = ',';
			}
			if (*p == '%' && !quote)
				stringkey = !stringkey;
			if (*p == '"' && !stringkey)
				quote = !quote;
			if (*p == sep && !quote && !stringkey) {
				*p = '\0';
				av[n++] = t;
				t = p + 1;
				sep = ',';
			}
		}
		av[n++] = t;

		(*func)(av, n);
		free(text);
	}

	return true;
}

static void
each_version(char *av[], size_t n)
{
	int d, m, y;

	/*
	 * Version Section. Match lines in the format of
	 *
	 *	DriverVer = <date>, <version>
	 */

	if (n > 2 && strcasecmp("DriverVer", av[0]) == 0) {
		if (sscanf(av[1], "%d/%d/%d", &m, &d, &y) == 3)
			easprintf(&DriverDate, "%04d-%02d-%02d", y, m, d);

		DriverVersion = estrdup(av[2]);
	}
}

static void
each_addreg(char *av[], size_t n)
{
	/*
	 * AddReg Section. Match lines in the format of
	 *
	 *	<?>,<?>,%RAMPatchFileName%,<flags>,<filename>
	 *
	 * and copy the value to RAMPatchFileName
	 */

	if (n > 5 && strcasecmp(av[3], "%RAMPatchFileName%") == 0)
		model_add(VendorID, ProductID, av[5]);
}

static void
each_hw(char *av[], size_t n)
{
	/*
	 * Match lines in the format of
	 *
	 *	AddReg=<addreg-section>
	 *
	 * and parse those sections
	 */

	if (n > 1 && strcasecmp("AddReg", av[0]) == 0)
		section_foreach(av[1], each_addreg);
}

static void
each_model(char *av[], size_t n)
{
	const char *ext[] = { "", ".nt", ".ntx86", ".ntia64", ".ntamd64" };
	char name[256];
	size_t i;

	/*
	 * Models Section. Match lines in the format of
	 *
	 *	<model-description> = <model-section-name>, <usb-device-id>
	 *
	 * and parse sections named
	 *
	 *	 <model-section-name>[|.nt|.ntx86|.ntia64|.ntamd64].hw
	 */

	if (n < 3 || sscanf(av[2], "USB\\VID_%4x&PID_%4x", &VendorID, &ProductID) != 2)
		return;

	for (i = 0; i < __arraycount(ext); i++) {
		snprintf(name, sizeof(name), "%s%s.hw", av[1], ext[i]);
		if (section_foreach(name, each_hw))
			break;
	}
}

static void
each_manufacturer(char *av[], size_t n)
{
	char name[256];
	size_t i;

	/*
	 * Manufacturer Section. Match lines in the format of
	 *
	 *	<description> = <manufacturer> [, <target> ...]
	 *
	 * parse sections named
	 *	<manufacturer> <manufacturer>.<target> ...
	 */
	if (n < 2 && strlen(av[0]) > 0)
		return;

	snprintf(name, sizeof(name), "%s", av[1]);
	for (i = 2;; i++) {
		section_foreach(name, each_model);

		if (i == n)
			break;

		snprintf(name, sizeof(name), "%s.%s", av[1], av[i]);
	}
}

static void
fw_install(void)
{
	struct model *m;
	char *path;
	FILE *i, *s, *d;
	int ch;

	easprintf(&path, "%s/index.txt", fwdir);
	i = efopen(path, "w");
	free(path);

	fprintf(i,  "#\n"
		    "# THIS FILE AUTOMATICALLY GENERATED - DO NOT EDIT\n"
		    "#\n"
		    "# Broadcom Driver version %s dated %s\n"
		    "\n", DriverVersion, DriverDate);

	for (m = models; m != NULL; m = m->next) {
		s = fopen(m->file, "r");
		if (s == NULL) {
			warn("%s", m->file);
			continue;
		}

		easprintf(&path, "%s/%s", fwdir, m->file);
		d = fopen(path, "wx");
		if (d == NULL && errno != EEXIST)
			err(EXIT_FAILURE, "%s", path);

		fprintf(i, "%04x:%04x\t%s\n", m->vid, m->pid, m->file);
		free(path);

		if (d == NULL)
			continue;

		while ((ch = fgetc(s)) != EOF)
			fputc(ch, d);

		fclose(s);
		fclose(d);

		nfiles++;
	}

	fclose(i);
}

int
main(int argc, char *argv[])
{
	struct dirent *de;
	DIR *dp;
	int len;

	if (argc > 2)
		err(EXIT_FAILURE, "usage: %s [source-dir]", getprogname());

	if (argc > 1 && chdir(argv[1]) == -1)
		err(EXIT_FAILURE, "%s", argv[1]);

	dp = opendir(".");
	if (dp == NULL)
		err(EXIT_FAILURE, "can't open directory");

	nfiles = 0;
	nmodels = 0;

	while ((de = readdir(dp)) != NULL) {
		if ((len = strlen(de->d_name)) > 4
		    && strcasecmp(&de->d_name[len - 4], ".inf") == 0) {

			models = NULL;
			sections = NULL;
			DriverDate = NULL;
			DriverVersion = NULL;

			read_inf(de->d_name);

			section_foreach("Version", each_version);
			section_foreach("Manufacturer", each_manufacturer);

			fw_install();

			break;
		}
	}

	closedir(dp);

	printf("%d firmware file%s installed for %d model%s to %s\n",
	    nfiles, (nfiles == 1 ? "" : "s"),
	    nmodels, (nmodels == 1 ? "" : "s"),
	    fwdir);

	return 0;
}
