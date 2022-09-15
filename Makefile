shell: shell.o
	gcc -o shell shell.o -lm
	rm -f shell.o
shell.o: shell.c
	gcc -c shell.c
clean: 
	rm -f shell
