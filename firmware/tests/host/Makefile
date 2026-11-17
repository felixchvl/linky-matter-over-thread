# SPDX-License-Identifier: Apache-2.0

SRC := ../../src/tic
CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I$(SRC)

test: test_tic
	./test_tic

test_tic: test_tic.c $(SRC)/tic_parser.c $(SRC)/tic_meter.c $(SRC)/tic_parser.h $(SRC)/tic_meter.h
	$(CC) $(CFLAGS) -o $@ test_tic.c $(SRC)/tic_parser.c $(SRC)/tic_meter.c

clean:
	rm -rf test_tic test_tic.dSYM

.PHONY: test clean
