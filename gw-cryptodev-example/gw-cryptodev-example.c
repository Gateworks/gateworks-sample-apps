/*
 * Copyright (C) 2016  Gateworks Corporation
 * Author: Pushpal Sidhu <psidhu@gateworks.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <stdio.h>		/* std io */
#include <string.h>		/* strstr */
#include <stdlib.h>		/* exit etc */
#include <stdarg.h>		/* va_start/va_end */
#include <getopt.h>		/* arg parsing */
#include <fcntl.h>		/* open() */
#include <unistd.h>		/* close() */
#include <sys/ioctl.h>		/* ioctl */
#include <crypto/cryptodev.h>	/* cryptodev header */
#include <stdint.h>		/* std types */

/* Program defines */
#define VERSION "1.0"

#define PLAINTEXT "Hello, World!"
#define AES_BLOCK_SIZE 16
#define KEY_SIZE       16

/* Program Types */
struct crypt_info {
	int fd;			     /* open(/dev/crypto) */
	struct session_op session;   /* crypto information */
	struct session_info_op siop; /* crypto session info */
};

/* Global Variables */
static unsigned int g_dbg = 0;

#define dbg(lvl, fmt, ...) _dbg (__func__, __LINE__, lvl, fmt, ##__VA_ARGS__)
void _dbg(const char *func, unsigned int line,
	  unsigned int lvl, const char *fmt, ...)
{
	if (g_dbg >= lvl) {
		va_list ap;
		printf("[%d]:%s:%d - ", lvl, func, line);
		va_start(ap, fmt);
		vprintf(fmt, ap);
		fflush(stdout);
		va_end(ap);
	}
}

/* aes_init - initialize cryptodev to use aes-cbc cipher */
static int aes_init(struct crypt_info *ci, const uint8_t *key, unsigned int key_size)
{
	dbg(2, "Configuring cryptodev to use CRYPTO_AES_CBC cipher\n");
	/* tell cryptodev to use an aes_cbc cipher with our input info*/
	ci->session.cipher = CRYPTO_AES_CBC;
	ci->session.keylen = key_size;
	ci->session.key = (void *)key;
	if (ioctl(ci->fd, CIOCGSESSION, &ci->session) < 0) {
		perror("ioctl");
		dbg(0, "Couldn't configure CRYPTO_AES_CBC cipher\n");
		return -1;
	}

	ci->siop.ses = ci->session.ses;
	if (ioctl(ci->fd, CIOCGSESSINFO, &ci->siop) < 0) {
		perror("ioctl");
		dbg(0, "Couldn't configure session_info_op\n");
		return -1;
	}

	if (strstr(ci->siop.cipher_info.cra_driver_name, "-caam")) {
		printf("Using %s driver! Accelerated through SEC4 engine.\n",
		       ci->siop.cipher_info.cra_driver_name);
	} else {
		printf("Using %s driver, not accelerated using SEC4 engine.\n",
		       ci->siop.cipher_info.cra_driver_name);
	}

	return 0;
}

static void aes_deinit(struct crypt_info *ci) {
	if (ioctl(ci->fd, CIOCFSESSION, &ci->session.ses) < 0) {
		perror("ioctl");
		dbg(0, "Couldn't deinitialize session\n");
	}
}

static int check_alignment(struct crypt_info *ci,
			   const void *plaintext, const void *ciphertext)
{
	/* check plaintext and ciphertext alignment */
	if (ci->siop.alignmask) {
		void *p;

		p = (void *)(((unsigned long)plaintext + ci->siop.alignmask) &
			     ~ci->siop.alignmask);
		if (plaintext != p) {
			fprintf(stderr, "plaintext is not aligned to %d\n",
				ci->siop.alignmask);
			return -1;
		}

		p = (void *)(((unsigned long)ciphertext + ci->siop.alignmask) &
			     ~ci->siop.alignmask);
		if (ciphertext != p) {
			fprintf(stderr, "ciphertext is not aligned to %d\n",
				ci->siop.alignmask);
			return -1;
		}
	}

	return 0;
}

static int aes_encrypt(struct crypt_info *ci, const void *iv,
		const void *plaintext, void *ciphertext, size_t size)
{
	struct crypt_op cop;

	if(check_alignment(ci, plaintext, ciphertext) < 0)
		return -1;

	/* Setup and start encryption */
	dbg(3, "cop memset to 0\n");
	memset(&cop, 0, sizeof(cop));

	dbg(2, "Placing result of encryption into ciphertext\n");
	cop.ses = ci->session.ses;
	cop.len = size;
	cop.iv  = (void *)iv;
	cop.src = (void *)plaintext;
	cop.dst = ciphertext;
	cop.op  = COP_ENCRYPT;
	if (ioctl(ci->fd, CIOCCRYPT, &cop) < 0) {
		perror("ioctl");
		dbg(0, "Encryption of plaintext failed\n");
		return -1;
	}

	return 0;
}

static int aes_decrypt(struct crypt_info *ci, const void *iv,
		const void *ciphertext, void *plaintext, size_t size)
{
	struct crypt_op cop;

	if(check_alignment(ci, plaintext, ciphertext) < 0)
		return -1;

	/* Setup and start decryption */
	dbg(3, "cop memset to 0\n");
	memset(&cop, 0, sizeof(cop));

	dbg(2, "Placing result of decryption into plaintext\n");
	cop.ses = ci->session.ses;
	cop.len = size;
	cop.iv  = (void *)iv;
	cop.src = (void *)ciphertext;
	cop.dst = plaintext;
	cop.op  = COP_DECRYPT;
	if (ioctl(ci->fd, CIOCCRYPT, &cop) < 0) {
		perror("ioctl");
		dbg(0, "Decryption of ciphertext failed\n");
		return -1;
	}

	return 0;
}

static int aes_test(struct crypt_info *ci)
{
	uint8_t plaintext[AES_BLOCK_SIZE] = PLAINTEXT;
	uint8_t ciphertext[AES_BLOCK_SIZE];
	uint8_t cp_ciphertext[AES_BLOCK_SIZE + 1];
	uint8_t iv[AES_BLOCK_SIZE];
	uint8_t key[KEY_SIZE] = "super-duper-key";

	if (aes_init(ci, key, sizeof(key)) < 0)
		return -1;

	/* We'll set iv to all 0's for this example */
	dbg(3, "iv memset to 0\n");
	memset(&iv, 0, sizeof(iv));

	aes_encrypt(ci, &iv, &plaintext, &ciphertext, AES_BLOCK_SIZE);

	memcpy(&cp_ciphertext, &ciphertext, sizeof(ciphertext));
	cp_ciphertext[AES_BLOCK_SIZE] = '\0';
	printf("Encrypted '%s' to '%s'\n",
	       (char *)&plaintext, (char *)&cp_ciphertext);

	/* Clear plaintext out to reuse variable */
	dbg(3, "plaintext memset to \\0\n");
	memset(&plaintext, '\0', sizeof(plaintext));

	aes_decrypt(ci, &iv, &ciphertext, &plaintext, AES_BLOCK_SIZE);
	printf("Decrypted '%s' to '%s'\n",
	       (char *)&cp_ciphertext, (char *)&plaintext);

	if (strcmp(PLAINTEXT, (char *)&plaintext) == 0)
		printf("Test passed!\n");
	else
		printf("Test failed!\n");

	aes_deinit(ci);
	return 0;
}

int main(int argc, char **argv)
{
	struct crypt_info ci;
	int rv;

	/* Argument vars */
	const struct option long_opts[] = {
		{"help",             no_argument,       0, '?'},
		{"version",          no_argument,       0, 'v'},
		{"debug",            required_argument, 0, 'd'},
		{ /* Sentinel */ }
	};
	char *arg_parse = "?hd:";
	const char *usage =
		"Usage: gw-cryptodev-example [OPTIONS]\n\n"
		"Options:\n"
		" --help,    -? - This usage\n"
		" --version, -v - Program Version: " VERSION "\n"
		" --debug,   -d - Debug Level (default: 0)\n"
		;

	/* Parse Args */
	while (1) {
		int opt_ndx;
		int c = getopt_long(argc, argv, arg_parse, long_opts, &opt_ndx);

		if (c < 0)
			break;

		switch (c) {
		case 0: /* long-opts only parsing */
			puts(usage);
			exit(EXIT_FAILURE);
		case 'h': /* Help */
		case '?':
			puts(usage);
			return 0;
		case 'v': /* Version */
			puts("Program Version: " VERSION);
			return 0;
		case 'd': /* debug level */
			g_dbg = atoi(optarg);
			dbg(1, "set debug level to: %d\n", g_dbg);
			break;
		default:
			puts(usage);
			exit(EXIT_FAILURE);
		}
	}

	dbg(3, "ci memset to 0\n");
	memset(&ci, 0, sizeof(ci));

	/* Open the crypto device */
	if ((ci.fd = open("/dev/crypto", O_RDWR, 0)) < 0) {
		perror("open");
		dbg(0, "Opening /dev/crypto with O_RDWR failed\n");
		exit(EXIT_FAILURE);
	}

	rv = aes_test(&ci);

	if (close(ci.fd) < 0) {
		perror("close");
		dbg(0, "Closing /dev/crypto failed\n");
		exit(EXIT_FAILURE);
	}

	return rv;
}
