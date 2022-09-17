shell: shell.o
	gcc -o $@ $^ -lm

%.o: %.c
	gcc -c $< -o $@

.PTHONY: clean
clean: 
	rm -f shell *.o 
