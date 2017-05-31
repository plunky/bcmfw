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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <unistd.h>
#include <util.h>

#include "bcmfw.h"

static uint8_t		inbuf[1024];
static uint8_t *	inptr;
static ssize_t		inmax;
static int		infd;

static uint8_t		cksum;

/*
 * Read 'Intel HEX' file, lines in the format:
 *
 *	:<Count><Addr><Type><Data1><Data2>...<DataN><Checksum>\r\n
 *
 * <Count>:	2 digits, the number of data bytes in the line
 * <Addr>:	4 digits, the address to write the data
 * <Type>:	2 digits, indicating the record type
 * <Data>:	<Count> * 2 digits of data
 * <Checksum>:	2 digits, to validate the line
 *
 * all digits are hexadecimal
 *
 * Also see:  https://en.wikipedia.org/wiki/Intel_HEX
 */
static char
read_char(void)
{
	
	if (inptr - inbuf == inmax) {
		inmax = read(infd, inbuf, sizeof(inbuf));
		inptr = inbuf;
		if (inmax == 0)
			return 0;
		if (inmax == -1)
			err(EXIT_FAILURE, "read");
	}

	return *inptr++;
}

static inline uint8_t
read_digit(void)
{
	char ch = read_char();

	if ('0' <= ch && ch <= '9')
		return ch - '0';

	if ('a' <= ch && ch <= 'f')
		return ch - 'a' + 0xa;

	if ('A' <= ch && ch <= 'F')
		return ch - 'A' + 0xa;

	if (ch == '\r' || ch == '\n')
		errx(EXIT_FAILURE, "unexpected EOL");

	if (ch == 0)
		errx(EXIT_FAILURE, "unexpected EOF");

	errx(EXIT_FAILURE, "invalid hex digit");
}

static inline uint8_t
read_byte(void)
{
	uint8_t v;
	
	v = (read_digit() << 4) + read_digit();
	cksum += v;
	return v;
}

struct ihex *
read_ihex(const char *infile)
{
	struct ihex *head, *block, *prev;
	uint32_t base;
	uint16_t addr;
	uint8_t type, count;
	uint8_t data[UINT8_MAX];
	char ch;
	int i;

	infd = open(infile, O_RDONLY);
	if (infd == -1)
		return NULL;

	inptr = inbuf;
	inmax = 0;
	cksum = 0;

	head = NULL;
	prev = NULL;
	base = 0;

	ch = read_char();
	for(;;) {
		if (ch != ':')
			errx(EXIT_FAILURE, "no start code");

		count = read_byte();
		addr = (read_byte() << 8) + read_byte();
		type = read_byte();

		for (i = 0; i < count; i++)
			data[i] = read_byte();

		(void)read_byte(); /* this byte ensures cksum == 0 */
		if (cksum != 0)
			errx(EXIT_FAILURE, "checksum mismatch");

		while ((ch = read_char()) == '\r' || ch == '\n')
			;

		switch (type) {
		case 0x00:	/* Data */
			block = emalloc(sizeof(struct ihex));

			if (count + sizeof(uint32_t) > UINT8_MAX)
				errx(EXIT_FAILURE, "ihex block too large");

			le32enc(block->data, base + addr);
			memcpy(block->data + sizeof(uint32_t), data, count);
			block->count = count + sizeof(uint32_t);
			block->next = NULL;

			if (head == NULL)
				head = block;
			if (prev != NULL)
				prev->next = block;

			prev = block;

			if (verbose > 1) {
				printf("  Data address 0x%08x, count %u",
				    le32dec(block->data), block->count);

				for (i = 4; i < block->count; i++) {
					printf("%s %02x",
					    (((i - 4) % 16) ? "" : "\n   "),
					    block->data[i]);
				}

				printf("\n");
			}
			break;

		case 0x01:	/* End of File */
			if (count != 0)
				errx(EXIT_FAILURE, "EOF: invalid data");

			if (ch != 0)
				errx(EXIT_FAILURE, "EOF: not end of file");

			close(infd);

			if (verbose > 1) {
				printf("\n");
			}

			return head;

		case 0x04:	/* Extended Linear Address */
			if (count != 2)
				errx(EXIT_FAILURE, "ELA: invalid data");

			base = (data[0] << 24) + (data[1] << 16);
			if (verbose > 1) {
				printf("  Extended Linear Address 0x%08x\n", base);
			}
			break;

		case 0x02:	/* Extended Segment Address */
		case 0x03:	/* Start Segment Address */
		case 0x05:	/* Start Linear Address */
		default:
			warnx("unhandled record type 0x%02x\n", type);
			break;
		}
	}
}
