#include <stdio.h>
#include <arm-mmio.h>
#include <inttypes.h>
#include <getopt.h>
#include <math.h>

#define REG_IDX_FIR_INFO   0
#define REG_IDX_COEFF_ADDR 1
#  define COEFF_ADDR_FDI   (1<<31) /* address FDI coefficients if set, FIR if clear */
#define REG_IDX_COEFF_DATA 2
#define REG_IDX_FDI_INFO   4
#define REG_IDX_FDI_NUM    5
#define REG_IDX_FDI_DEN    6

/* expect  */
#define CLEN 16

#define FDI_LEN 5
#define FDI_ORD 3

static void
usage(const char *nm)
{
	fprintf(stderr,"Usage: %s [-v] [-B] [-d dev] [-f coeff-file] [-N numerator -D denominator] [-h]\n", nm);
	fprintf(stderr,"          program FIR or FDI filter coefficients\n");
	fprintf(stderr,"          If -N/-D (both) are given then the FDI is programmed, otherwise the FIR\n");
	fprintf(stderr,"      -B: set coefficients to effectively bypass FDI\n");
}

static int
prog_coeffs(Arm_MMIO m, int16_t *coeffs, int ncoeffs, int is_fdi)
{
int i;
uint32_t a = is_fdi ?  COEFF_ADDR_FDI : 0;
uint32_t d;
	for ( i=0; i<ncoeffs; i++ ) {
		iowrite32(m, REG_IDX_COEFF_ADDR, a+i);
		iowrite32(m, REG_IDX_COEFF_DATA, (int32_t)coeffs[i]);
		iowrite32(m, REG_IDX_COEFF_ADDR, a+i);
		d = ioread32(m, REG_IDX_COEFF_DATA);
		if ( (int16_t)(d&((1<<CLEN)-1)) != coeffs[i] ) {
			fprintf(stderr,"Coefficient readback failed (i=%i, got %"PRIx32", expected %"PRIx16"\n", i, d, coeffs[i]);
			return -1;
		}
	}
	return 0;
}


/* (Unscaled) coefficients for FDI of order 3, FIR/pipeline length 5.
 * The layout is: 
 *
 *   order 3, (negative/future) delay 2
 *   order 3, (negative/future) delay 1
 *   order 3, (negative/future) delay 0
 *
 *   order 2, (negative/future) delay 2
 *   order 2, (negative/future) delay 1
 *   order 2, (negative/future) delay 0
 *   ...
 *   order 0, (negative/future) delay 2
 *   order 0, (negative/future) delay 1
 *   order 0, (negative/future) delay 0
 *
 * Due to symmetry the coefficients for positive delays (past samples)
 * are not required.
 */
static double fdi_5_4_coeffs[12] = {
       0.157929,
      -0.279339,
              0,
     -0.0503056,
       0.687499,
       -1.27584,
      -0.144182,
       0.758819,
              0,
   -0.000378603,
     0.00139179,
       0.997961
};

#define CMAX 2048
#define DMAX 30

int
main(int argc, char **argv)
{
const char *devnm = "/dev/uio3";
int opt;
int   rval = 1;
const char *fnam  = 0;
FILE  *f = 0;
int16_t coeffs[CMAX];
double  dcoeffs[DMAX];
int   ncoeffs = 0;
int   ci;
int   i,j;
int   verb = 0;
Arm_MMIO m = 0;

uint32_t a = 0, d,d1;
int      isfdi = 0;
uint32_t num = 0, den = 0;
long     l;
uint32_t *ap;
double   scl,sclp, max;
int      bypass_fdi = 0;

	while ( (opt = getopt(argc, argv, "vhBd:f:N:D:")) > 0 ) {
		ap = 0;
		switch ( opt ) {
			case 'h': rval = 0;
			default:
				usage(argv[0]);
				return rval;

			case 'd': devnm = optarg; break;
		
			case 'f': fnam  = optarg; break;

			case 'N': ap = &num; break;
			case 'D': ap = &den; break;

			case 'B': bypass_fdi = 1; num = den = 128; break;

			case 'v': verb = 1; break;
		}
		if ( ap ) {
			if ( 1 != sscanf(optarg,"%li",&l) ) {
				fprintf(stderr,"Error: unable to parse argument to %c option: %s\n", opt, optarg);
				return rval;
			}
			*ap = (uint32_t)l;
		}
	}

	if ( (0 == num) != (0 == den) ) {
		fprintf(stderr,"Error: You must give either both, num AND den (to program FDI) or none (to program FIR only)\n");
		return rval;
	}

	if ( den > (1<<(CLEN-1))-1 ) {
		fprintf(stderr,"Error: Max denominator is %u\n", (1<<(CLEN-1))-1);
		return rval;
	}

	if ( fnam ) {
		if ( ! strcmp(fnam, "-") ) {
			f = stdin;
		} else if (  !(f = fopen(fnam,"r")) ) {
			perror("Opening file for reading");
			return rval;
		}
	}

	if ( ! (m = arm_mmio_init(devnm)) ) {
		fprintf(stderr,"Unable to open MMIO\n");
		goto bail;
	}

	d  = ioread32(m, REG_IDX_FIR_INFO);
	if ( verb ) {
		printf("FIR/FDI Coefficient length:     %4i\n", (d>>16));
		printf("# (symmetric) FIR Coefficients: %4i\n", (1<<((d&0xffff)-1)));
	}

	d1 = ioread32(m, REG_IDX_FDI_INFO);

	i=((d1>>4) & 0xf);
	if ( verb ) {
		printf("FDI Length: %2i\n", i);
	}

	if ( i != FDI_LEN ) {
		fprintf(stderr,"Error: FDI length %i, expected %i\n", i, FDI_LEN);
		goto bail;
	}

	i=((d1>>0) & 0xf);
	if ( verb ) {
		printf("FDI Order:  %2i\n", i);
	}

	if ( i != FDI_ORD ) {
		fprintf(stderr,"Error: FDI order %i, expected %i\n", i, FDI_ORD);
		goto bail;
	}

	if ( d>>16 != CLEN ) {
		fprintf(stderr,"Error: Coefficient lenght != %i\n", CLEN);
		goto bail;
	}

	if ( f ) {
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

		i = (1<<((d&0xffff)-1));
		if ( ncoeffs != i ) {
			fprintf(stderr,"File contains %i coefficients, expected %i\n", ncoeffs, i);
			goto bail;
		}

		if ( prog_coeffs(m, coeffs, ncoeffs, 0 ) )
			goto bail;

	}

	if ( num ) {
		// scale num + den up...
		while ( ! (den & (1<<(CLEN-2))) ) {
			den <<=1;
			num <<=1;
		}
		// pre-scale FDI coefficients by 2^(CLEN-1)/D
		ncoeffs = sizeof(fdi_5_4_coeffs)/sizeof(fdi_5_4_coeffs[0]);

		if ( bypass_fdi ) {
			for ( i=0; i<ncoeffs; i++ )
				dcoeffs[i] = 0;
			dcoeffs[ncoeffs-1] = 1.0;
			max = 1.0;
		} else {
			scl  = (double)(1<<(CLEN-1))/(double)den;
			sclp = 1.0;
			max = 0.;
			i    = ncoeffs-1;
			j    = 0;
			while ( i >= 0 ) {
				dcoeffs[i] = fdi_5_4_coeffs[i]*sclp;
#if 0
				printf("%2i: %10g %10g %10g\n", i, dcoeffs[i], fdi_5_4_coeffs[i], sclp); 
#endif
				if ( fabs(dcoeffs[i]) > max )
					max = fabs(dcoeffs[i]);
				if ( ++j == (FDI_LEN+1)/2 ) {
					j     = 0;
					sclp *= scl;
				}
				i--;
			}
		}
		scl = (double)((1<<(CLEN-1))-2) / max;
		if ( verb ) {
			printf("FDI Coefficients scl %g, max %g\n", scl, max);
			printf("FDI Coefficients (NUM: %"PRIu32", DEN: %"PRIu32")\n", num, den);
		}
		for ( i=0; i<ncoeffs; i++ ) {
			coeffs[i] = (int16_t)rint(dcoeffs[i]*scl);
			if ( verb ) {
				// bug? with xilinx compiler's inttypes? I get ugly printouts (2013.4)
				printf("%12.2lf 0x%04"PRIx16" -- %6"PRIi16"\n", dcoeffs[i]*scl, (coeffs[i] & ((1<<CLEN)-1)), coeffs[i]);
			}
		}

		if ( prog_coeffs(m, coeffs, ncoeffs, 1) )
			goto bail;

		iowrite32(m, REG_IDX_FDI_NUM, num);
		iowrite32(m, REG_IDX_FDI_DEN, den);
	}

bail:
	if ( fnam && f )
		fclose(f);
	if ( m )
		arm_mmio_exit(m);
	return rval;
}