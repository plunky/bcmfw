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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bcmfw.h"

static const char bcmfw_dir[] = BCMFW_DIR;

int	verbose = 1;

static void
usage(void)
{

	fprintf(stderr,
	    "usage: %s [-qv] [-f firmware] [-m mini-driver] [device ...]\n",
	    getprogname()
	);

	fprintf(stderr,
	    "Where:\n"
	    "\t-q              be quiet\n"
	    "\t-v              be verbose\n"
	    "\t-f firmware     for BCM2033, via ugen\n"
	    "\t-m mini-driver  for BCM2033, via ugen\n"
	);

	exit(EXIT_FAILURE);
}

int
main (int argc, char **argv)
{
	int ch, n;

	while ((ch = getopt(argc, argv, "f:m:qv")) != -1) {
		switch (ch) {
		case 'f':	/* firmware file (BCM2033) */
			bcm2033_fw = optarg;
			break;

		case 'm':	/* minidriver file (BCM2033) */
			bcm2033_md = optarg;
			break;

		case 'q':	/* quiet mode */
			verbose = 0;
			break;

		case 'v':	/* verbose mode */
			verbose++;
			break;

		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (chdir(bcmfw_dir) == -1)
		warn("%s", bcmfw_dir);

	/*
	 * For compatibility with previous versions, we allow devices
	 * to be listed on the command line. These can be either ugen
	 * (signifying BCM2033) or a Bluetooth devname. If no devname
	 * was given, then we check all the adaptors present.
	 */
	n = 0;
	while (argc > 0) {
		if (strncmp(*argv, "ugen", 4) == 0) {
			check_ugen(*argv);
		} else {
			check_btdev(*argv);
			n++;
		}

		argc--;
		argv++;
	}

	if (n == 0)
		check_btdev(NULL);

	return 0;
}
