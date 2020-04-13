CFLAGS = -g -O -I/usr/unclude -L/usr/lib -Wno-implicit-function-declaration
LIBS = -lm

OBJ =\
	dynamite_control.o

all: dynamite_control

clean:
	@$(RM) -f *.o dynamite_control

install:
	@$(foreach file, $(wildcard dynamite_control), cp -rf $(file) /usr/bin;)

.c.o:
	@$(CC) -c $(CFLAGS) $<

dynamite_control: $(OBJ)
	@$(CC) $(OBJ) $(LIBS) -o dynamite_control
