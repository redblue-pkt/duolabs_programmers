CFLAGS = -g -O -I/usr/unclude -L/usr/lib
LIBS = -lm

OBJ =\
	dynamite_control.o

all: dynamite_control

clean:
	@rm -f *.o dynamite_control
.c.o:
	@$(CC) -c $(INCLUDES) $(CFLAGS) $<

dynamite_control: $(OBJ)
	@$(CC) $(OBJ) $(LIBS) -o dynamite_control
