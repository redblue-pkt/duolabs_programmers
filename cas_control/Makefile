CFLAGS = -g -O -I/usr/unclude -L/usr/lib -Wno-implicit-function-declaration
LIBS = -lm

OBJ =\
	cas_control.o

all: cas_control

clean:
	@$(RM) -f *.o cas_control

install:
	@$(foreach file, $(wildcard cas_control), cp -rf $(file) /usr/bin;)

.c.o:
	@$(CC) -c $(CFLAGS) $<

dynamite_control: $(OBJ)
	@$(CC) $(OBJ) $(LIBS) -o cas_control
