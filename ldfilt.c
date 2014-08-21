#include <stdio.h>
#include <arm-mmio.h>
#include <inttypes.h>
#include <getopt.h>

static void
usage(const char *nm)
{
	fprintf(stderr,"Usage: %s [-d dev] [-f coeff-file] [-h]\n", nm);
	fprintf(stderr,"          program FIR filter coefficients\n");
}

#define CMAX 2048

int
main(int argc, char **argv)
{
const char *devnm = "/dev/uio3";
int opt;
int   rval = 1;
const char *fnam  = 0;
FILE  *f = stdin;
int16_t coeffs[CMAX];
int   ncoeffs = 0;
int   ci;
int   i;
Arm_MMIO m = 0;
uint32_t a,d;

	while ( (opt = getopt(argc, argv, "hd:f:")) > 0 ) {
		switch ( opt ) {
			case 'h': rval = 0;
			default:
				usage(argv[0]);
				return rval;

			case 'd': devnm = optarg; break;
		
			case 'f': fnam  = optarg; break;
		}
	}

	if ( fnam && !(f = fopen(fnam,"r")) ) {
		perror("Opening file for reading");
		return rval;
	}

	if ( ! (m = arm_mmio_init(devnm)) ) {
		fprintf(stderr,"Unable to open MMIO\n");
		goto bail;
	}

	d = ioread32(m, 0);

	if ( d>>16 != 16 ) {
		fprintf(stderr,"Error: Coefficient lenght != 16\n");
		goto bail;
	}

	while ( ncoeffs < sizeof(coeffs)/sizeof(coeffs[0]) && ! feof(f) ) {
		if ( 1 != fscanf(f,"%x", &ci) ) {
			if ( feof(f) )
				break;
			fprintf(stderr,"Error reading integer (line %i)\n", ncoeffs+1);
			goto bail;
		}
		coeffs[ncoeffs] = (int16_t)(uint16_t)(ci&0xffff);
		ncoeffs++;
	}

	if ( ncoeffs != 1<<((d&0xffff)-1) ) {
		fprintf(stderr,"File contains %i coefficients, expected %i\n", ncoeffs, (1<<((d&0xffff)-1)));
		goto bail;
	}

	for ( i=0; i<ncoeffs; i++ ) {
		iowrite32(m, 1, i);
		iowrite32(m, 2, (int32_t)coeffs[i]);
		iowrite32(m, 1, i);
		d = ioread32(m, 2);
		if ( (int16_t)(d&0xffff) != coeffs[i] ) {
			fprintf(stderr,"Coefficient readback failed (i=%i, got %"PRIx32", expected %"PRIx16"\n", i, d, coeffs[i]);
			goto bail;
		}
	}

bail:
	if ( fnam && f )
		fclose(f);
	if ( m )
		arm_mmio_exit(m);
	return rval;
}
