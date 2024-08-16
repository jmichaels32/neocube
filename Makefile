neocube: cube.o util.o
	gcc cube.o util.o -o neocube

cube.o: cube.c
	gcc -c cube.c -o cube.o

util.o: lib/util.c
	gcc -c lib/util.c -o util.o

# Linking
# gcc file1.o file2.o file3.o -o program

clean:
	rm -f *.o

reset:  
	rm -f *.o neocube
