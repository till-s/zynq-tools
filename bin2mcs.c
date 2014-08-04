#include <stdio.h>
#include <inttypes.h>
#include <getopt.h>
#include <string.h>

static const char *fmt00=":%02X%04"PRIX32"00";
static const char *fmt01=":00000001FF\n";
static const char *fmt04=":02000004";

#define NLIN 16

static void
prcs(uint32_t cs)
{
	printf("%02"PRIX8"\n", (uint8_t)(-cs)&0xff);
}

static void
usage(const char *nam)
{
const char *s_nam = strrchr(nam, '/'); 
	s_nam = s_nam ? s_nam + 1 : nam;
	fprintf(stderr,"usage: %s [-h] [-f file_name] -b <load_addr>\n", s_nam);
	fprintf(stderr,"                   convert binary file into\n");
	fprintf(stderr,"                   mcs-records on stdout\n\n");
	fprintf(stderr,"   -h              print this message\n\n");
	fprintf(stderr,"   -b  <load_addr> define load address\n\n");
	fprintf(stderr,"   -f  <file_name> read from file instead of stdin\n");
}

int
main(int argc, char *argv[])
{
uint32_t addr = 0, ah;
char    *fnam = 0;
uint8_t  buf[NLIN];
FILE    *f = 0;
size_t   got;
uint32_t cs = 0;
int      i;
int      rval = 1;
int      has_base = 0;

	while ( (i = getopt(argc, argv, "b:f:h") ) >= 0 ) {
		switch ( i ) {
			case 'b':
				if ( 1 != sscanf(optarg, "%"SCNi32, &addr) ) {
					fprintf(stderr,"Unable to parse address: %s\n", optarg);
					return 1;
				}
				has_base = 1;
			break;

			case 'f':
				fnam = optarg;
			break;

			default:
				fprintf(stderr,"Unknown option '%c'\n", i);
				rval++;
				/* fall thru */
			case 'h':
				rval--;
				usage(argv[0]);
				return rval;
		}
	}

	if ( ! has_base ) {
		fprintf(stderr,"Missing load address - must use -b option\n\n");
		usage(argv[0]);
		return 1;
	}

	if ( ! (f = (fnam ? fopen(fnam,"r") : stdin)) ) {
		perror("opening file");
		return 1;
	}

	while ( (got = fread(buf, 1, sizeof(buf), f)) > 0 ) {
		if ( 0 == (addr & 0xffff) ) {
			printf("%s",fmt04); cs = 6;
			ah = addr >> 16;
			cs += (ah >> 8 ) & 0xff;
			cs += (ah      ) & 0xff;
			printf("%04"PRIX32, ah);
			prcs(cs);
		}

		cs  = got;
		cs += addr & 0xff;
		cs += (addr >> 8) & 0xff;
		printf(fmt00, got, (addr & 0xffff));
		for ( i=0; i<got; i++ ) {
			cs += buf[i];
			printf("%02"PRIX8, buf[i]);
		}
		prcs(cs);

		addr += got;
	}

	if ( ferror(f) ) {
		perror("reading file");
		goto bail;
	}

	printf("%s",fmt01);

	rval = 0;

bail:
	if ( fnam )
		fclose(f);
	
	return rval;
}
