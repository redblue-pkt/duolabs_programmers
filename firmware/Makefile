all: cas2 cas2plus cas2plus2 cas2pluscrypto cas3 cas3plus dynamite dynamiteplus dynamiteplustiny fx1 fx2
	@for f in `find $^ ! -type d`; do ../ihex2fw/ihex2fw $$f $${f%%.*}.fw ; rm $$f ; done
	@$(foreach file, $(wildcard $^), cp -rf $(file) /lib/firmware;)

