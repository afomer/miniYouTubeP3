################################################################################
# Makefile                                                                     #
#                                                                              #
# Description: This file contains the make rules for Recitation 1.             #
#                                                                              #
# Authors: Abuabker Omer <afomer@cmu.edu>    			                       #
#                                                                              #
################################################################################
CC = gcc
CCFLAGS = -Wall -Werror

all: proxy

%.o : ./%.c
	$(CC) -c $(CCFLAGS) $<

proxy: proxy.o parser.o clients.o
	$(CC) $(CCFLAGS) proxy.o -o proxy

clean.o:
	@rm *.o proxy
