# Author: Daniel Dusek <xdusek21@stud.fit.vutbr.cz>
# POS Project 2017: Shell

TARGET = project.c

all:
	gcc -ansi -pedantic -Wall $(TARGET) -o project -lpthread -O -g
	@#rm project

clean:
	rm project





run:
	@./project

#reference-run:
#	@./project 1024 100

wis-pack:
	zip "xdusek21.zip" "project.c" "Makefile"

