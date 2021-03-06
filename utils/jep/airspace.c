/*
 * Airspace
 *
 * Airspace database generator
 *
 */

#include <stdio.h>
#include "PDBManager.h"

#define PI 		     	  3.1415926535897932384626433832795
#define NM_PER_RADIAN     ((180*60)/PI)

#define RAD_PER_DEG       (PI/180)
#define NM_TO_RAD(x)	  ((x)/NM_PER_RADIAN)
#define DEG_TO_RAD(x)	  (RAD_PER_DEG*(x))
#define RAD_TO_INT16(x)   (Int16) ( ((x) * 32767.0) / PI)
#define RAD_TO_INT32(x)   (Int32) ( (x) * 2147483647.0 / PI)
#define DEG_TO_INT32(x)   (Int32) ( (x) * 2147483647.0 / 180 )

#define MIN(x,y) ( (x) < (y) ? (x):(y) )
#define MAX(x,y) ( (x) > (y) ? (x):(y) )
#define ABS(a) ( (a) < 0 ? -a : a)

#define SKIP_FIELD(p) while((p)[0]>' ')(p)++;while ((p)[0]<'!') (p)++

typedef UInt16 AirspaceIDType;

typedef enum {

	altAmsl, altAgl, altFL, altNotam

} AltReferenceType;

typedef unsigned char UInt8;

/*
 * master class type, select one of these bits per airspace record. However
 * airways may be Low, High or Both
 *
 */

#define asTypeSUAS       0x0010
#define asTypeClassA     0x0020
#define asTypeClassB     0x0040
#define asTypeClassC     0x0080
#define asTypeClassD     0x0100
#define asTypeClassE     0x0200
#define asTypeClassF     0x0400
#define asTypeClassG     0x0800
#define asTypeLowAirway  0x1000
#define asTypeHighAirway 0x2000
#define asTypeOther      0x4000
#define asTypeClassBG   (asTypeClassB | asTypeClassC | asTypeClassD | asTypeClassE | asTypeClassF | asTypeClassG )
#define asTypeAirspace   (asTypeClassA | asTypeClassBG | asTypeSUAS)
#define asTypeAirway     (asTypeLowAirway | asTypeHighAirway)

#define asTypeMask       0xFFF0
#define asSubTypeMask    0x000F

/*
 * subclass types. 'or' these with the master class
 * type to produce AirspaceClassType
 * 
 */

#define suasAlert   0
#define suasDanger  1
#define suasMoa     2
#define suasProhibited 3
#define suasRestricted 4
#define suasTra     5
#define suasWarning 6

typedef UInt16 AirspaceClassType;


/*
 * line segment for airspace boundary
 *
 */

typedef struct {

	/*
	 * end of line (start is end of previous segment) 
	 *
	 */

	Int32 lat, lon;

} __attribute__((packed)) LineSegmentType;

/*
 * arc segment for airspace boundary
 *
 */

typedef struct {

	/*
	 * centre of arc 
	 *
	 */

	Int32 lat, lon;

	/*
	 * radius in latitude radians. If -ve then arc is anticlockwise.
	 *
	 * radius = (NM radius / 10800) * 2^31
	 *
	 */

	Int32 radius;

	/*
	 *  start & end angles of arc (radians)
	 *
	 *  if start=end=0 then arc is a circle.  
	 *
	 */

	Int16 start, end;

} __attribute__((packed)) ArcSegmentType;


/*
 * airspace record
 *
 */

typedef struct {

	AirspaceClassType type;
	  
	/*
	 * type-specific data
	 *
	 * For airspace, this is the primary frequency of the airspace. High
	 * byte is whole MHz, low byte is decimal part. E.g. 118.75 is stored
	 * as ( 118 << 8) + 75
	 *
	 * For airways, this is the RNP of the airway expressed as NM*10
	 *
	 */

	UInt16 extra;
	
	/*
	 * altitude limits in feet, and reference points
	 *
	 */

	UInt16 lowerAlt, upperAlt;
	UInt8 lowerAltRef;
	UInt8 upperAltRef;

	/*
	 * segmentCode is a null-terminated string of 'l' (line) or 'a' (arc)
	 * characters, describing the data that follows in the segData part of
	 * the record.
	 *
	 * E.g. 'llalla' means line, line, arc, line, line,arc.
	 *
	 */

	char segmentCode[1];

	/*
	 * textual description of the airspace, lines separated by \n, null
	 * terminated. Line 1 should be the name/description of the airspace.
	 * Lines 2 onwards are freeform. 
	 *
	 * The size of description + segmentCode should be padded to an even
	 * number of bytes (including \0), by appending a '\n' character here
	 * if necessary. DO NOT USE \0 TO PAD!
	 *
	 */

	char description[1];

	/*
	 * rest of record is set of ArcSegmentType and LineSegmentType records.
	 *
	 */

	LineSegmentType segData[1];

} __attribute__ ((packed)) AirspaceType;

/*
 * index is the first 5 records of the database
 * 
 * 0 = latitude 1
 * 1 = longitude 1
 * 2 = latitude 2
 * 3 = longitude 2
 * 4 = airspace type
 *
 * (1=top left, 2=bottom right)
 *
 */

enum {lat1IdxRec = 0, lon1IdxRec, lat3IdxRec, lon3IdxRec, typeIdxRec, lastIdxRec };


/*******************************************************************************
 *
 * functions
 *
 */


/*
 * function : AvCalcGreatCircleCourse
 *
 */

double AvCalcGreatCircleCourse(double lat1, double lon1,
		double lat2, double lon2, double *range) {

	double sinlat1 = sin(lat1);
	double sinlat2 = sin(lat2);
	double coslat1 = cos(lat1);
	double rng;
	double crs;

	rng=acos(sinlat1*sinlat2+coslat1*cos(lat2)*cos(lon1-lon2));

	if (fabs(lon1 - lon2) < 1e-12) {
		if (lat1>lat2)
			crs = PI;
		else
			crs = 0;
	} else {
		crs = acos((sinlat2-sinlat1*cos(rng))/(sin(rng)*coslat1));
		if (! (sin(lon2-lon1)<0))  {
			crs = 2*PI - crs;
		}
	}
	*range = rng;

	/*
	 * we're getting away without this on PalmOS!!! What luck!!!
	 *
	 */

	if (crs > PI) crs -= PI*2;

	return crs;
}

/*
 * function : ImportAltitude
 *
 * Reads and decodes altitude from intermediate format string
 *
 * A = AMSL
 * G = AGL
 * F = FL
 * B = BYNOTAM
 *
 */

static void ImportAltitude(const char *line, UInt8 *ar, UInt16 *alt) {

	switch (line[0]) {

	case 'A':
		*ar = altAmsl;
		break;

	case 'G':
		*ar = altAgl;
		break;

	case 'F':
		*ar = altFL;
		break;

	case 'B':
		*ar = altNotam;
		break;

	}

	*alt = (UInt16)atoi(&line[1]);

}

/*
 * function : ImportRecord
 *
 * Reads a single airspace record in intermediate format from the file and
 * creates an airspace record from it.
 *
 * lat1/lon1 and lat3/lon3 are the north-west and south-east boundaries of
 * the airspace
 *
 */

AirspaceType *ImportRecord(Int16 *lat1, Int16 *lon1, Int16 *lat3, Int16 *lon3, int *size) {

	char line[1024], name[256];
	char *description = malloc(4096);
	char *segStr = malloc(2400);
	Int16 segCount;
	AirspaceType *as = malloc(2400+2400*sizeof(LineSegmentType));
	void *segStart = malloc(2400*sizeof(LineSegmentType));
	void *segPtr   = segStart;
	void *writePtr;
	double blat1, blon1, blat2, blon2;


	/*
	 * type & class:
	 *
	 * A-G = class A to G
	 *
	 * S[ADMPRTW]= Special Use (SUAS)
	 *
	 * W[LHB] = Airway, (L)ow level, (H)igh level or Both
	 *
	 * O= Other
	 *
	 */

	if (!gets(line)) {

		free(as);
		free(description);
		free(segStart);
		free(segStr);

		return NULL;

	}

	switch (line[0]) {

	case 'A': as->type = asTypeClassA; break;
	case 'B': as->type = asTypeClassB;break;
	case 'C': as->type = asTypeClassC;break; 
	case 'D': as->type = asTypeClassD;break;
	case 'E': as->type = asTypeClassE;break;
	case 'F': as->type = asTypeClassF;break;
	case 'G': as->type = asTypeClassG;break;

	case 'S':
		switch (line[1]) {

		case 'A': as->type = suasAlert;
			  break;
		case 'D': as->type = suasDanger;
			  break;
		case 'M': as->type = suasMoa;
			  break;
		case 'P': as->type = suasProhibited;
			  break;
		case 'R': as->type = suasRestricted;
			  break;
		case 'T': as->type = suasTra;
			  break;
		case 'W': as->type = suasWarning;
			  break;

		}
		as->type |= asTypeSUAS;
		break;

	case 'W':
		switch(line[1]) {

		case 'L': as->type = asTypeLowAirway;
			  break;

		case 'H': as->type = asTypeHighAirway;
			  break;

		case 'B': as->type = asTypeHighAirway | asTypeLowAirway;
			  break;

		default:exit(113);
			break;

		}
		break;

	case 'O':
		as->type = asTypeOther;
		break;

	default:
		printf("Unknown airspace %s\n", line);
		exit(114);
		break;

	}

	/*
	 * name/description
	 *
	 */

	*description = 0;
	gets(line);
	while (line[0] != '-') {

		if (line[0] != 0) {

			strcat(description, line);
			strcat(description, "\n");

		}

		gets(line);

	}
	//printf ("%s\n", description);

	/*
	 * frequency or RNP
	 *
	 */

	gets(line);

	if (as->type & asTypeAirway) {

		as->extra = atoi(line);

	} else {
		
		as->extra = (atoi(line)*256+atoi(&line[4]));
		as->extra = 0;

	}

	/*
	 * lower & upper altitude
	 * 
	 */

	gets(line);
	ImportAltitude(line, &as->lowerAltRef, &as->lowerAlt);

	gets(line);
	ImportAltitude(line, &as->upperAltRef, &as->upperAlt); 

	
	/*
	 * boundary segments follow:
	 *
	 * L = Line:
	 *
	 * L<lat> <lon>
	 *
	 * A = Arc:
	 *
	 * A[LR]<lat2> <lon2> <lat3> <lon3> <lat1> <lon1>
	 *
	 * (L=left arc, R=right arc)
	 *
	 * (point 1 is centre, 2 and 3 are start and end points)
	 *
	 * C = Circle:
	 *
	 * C<lat> <lon> <radius>
	 *
	 * segStr is built and the bounding limits of the airspace (blat1 etc)
	 * are updated as the segments are read and processed
	 *
	 */

	blat1 = -PI/2;
	blon1 = -PI;
	blat2 = PI/2;
	blon2 = PI;

	segCount = 0;

	gets(line);

	while (line[0] != 'X') {

		const char *s = line+1;

		if (line[0] == 'L') {

			LineSegmentType l;
			double lat, lon;

			sscanf(s,"%lf %lf", &lat, &lon);

			lat = DEG_TO_RAD(lat);
			lon = -DEG_TO_RAD(lon);

			l.lat = RAD_TO_INT32(lat);
			l.lon = RAD_TO_INT32(lon);

			l.lat = PDBPalm32(l.lat);
			l.lon = PDBPalm32(l.lon);

			segStr[segCount] = 'l';
			*(LineSegmentType*)segPtr = l;
			segPtr += sizeof(LineSegmentType);

			blat1 = MAX(blat1, lat);
			blon1 = MAX(blon1, lon);
			blat2 = MIN(blat2, lat);
			blon2 = MIN(blon2, lon);

		} else if (line[0] == 'A') {

			double lat1, lon1;
			double lat2, lon2;
			double lat3, lon3;
			double radius,bearing;

			ArcSegmentType ar;

			s+=1;

			/*
			 * end points
			 *
			 */

			sscanf(s,"%lf %lf %lf %lf %lf %lf", &lat2, &lon2, &lat3, &lon3, &lat1, &lon1);
			lat1 = DEG_TO_RAD(lat1);
			lon1 = -DEG_TO_RAD(lon1);
			lat2 = DEG_TO_RAD(lat2);
			lon2 = -DEG_TO_RAD(lon2);
			lat3 = DEG_TO_RAD(lat3);
			lon3 = -DEG_TO_RAD(lon3);

			ar.lat = RAD_TO_INT32(lat1);
			ar.lon = RAD_TO_INT32(lon1);

			bearing = AvCalcGreatCircleCourse(lat1, lon1, lat2, lon2, &radius);
			ar.radius = RAD_TO_INT32(radius);
			ar.start  = RAD_TO_INT16(bearing);
			bearing = AvCalcGreatCircleCourse(lat1, lon1, lat3, lon3, &radius);
			ar.end    = RAD_TO_INT16(bearing);

			if (line[1] == 'L')
			   	ar.radius = -ar.radius;

			if (ar.start == -0x8000) ar.start = 0x4444;

			ar.lat = PDBPalm32(ar.lat);
			ar.lon = PDBPalm32(ar.lon);
			ar.radius = PDBPalm32(ar.radius);
			ar.start  = PDBPalm16(ar.start);
			ar.end  = PDBPalm16(ar.end);

			segStr[segCount] = 'a';
			*(ArcSegmentType*)segPtr = ar;
			segPtr += sizeof(ArcSegmentType);

			/*
			 * assume arc is a complete circle, and calculate
			 * bounding box accordingly
			 *
			 * latitude limits are easy - just add the
			 * radian-radius, and limit the max to +-90 degrees
			 *
			 */

			blat1 = MAX(blat1, MIN(PI/2, lat1+radius));
			blat2 = MIN(blat2, MAX(-PI/2,lat1-radius));

			/*
			 * longitude is more complicated, as it needs to wrap
			 * around.
			 *
			 * scale the radius according to our latitude
			 *
			 * remember west is +ve!!!
			 *
			 * Wrap around at 180deg W/E
			 *
			 */

			radius /= cos(lat1);

			lon1 += radius;
			if (lon1 > PI) lon1 -= 2*PI;
			blon1 = MAX(blon1, lon1);
			blon2 = MIN(blon2, lon1);

			lon1 -= 2*radius;
			if (lon1 < -PI) lon1 += 2*PI;
			blon1 = MAX(blon1, lon1);
			blon2 = MIN(blon2, lon1);

		} else if (line[0] == 'C') {

			double lat1, lon1;
			double radius;

			ArcSegmentType ar;

			/*
			 * centre
			 *
			 */
			
			sscanf(s,"%lf %lf %lf", &lat1, &lon1, &radius);
			lat1 = DEG_TO_RAD(lat1);
			lon1 = -DEG_TO_RAD(lon1);

			ar.lat = RAD_TO_INT32(lat1);
			ar.lon = RAD_TO_INT32(lon1);

			/*
			 * radius in nm
			 *
			 */

			radius = NM_TO_RAD(radius);
			ar.radius = RAD_TO_INT32(radius);
			ar.start  = 0;
			ar.end    = 0;

			ar.lat = PDBPalm32(ar.lat);
			ar.lon = PDBPalm32(ar.lon);
			ar.radius = PDBPalm32(ar.radius);

			segStr[segCount] = 'a';
			*(ArcSegmentType*)segPtr = ar;
			segPtr += sizeof(ArcSegmentType);

			/*
			 * latitude limits are easy - just add the
			 * radian-radius, and limit the max to +-90 degrees
			 *
			 */

			blat1 = MAX(blat1, MIN(PI/2, lat1+radius));
			blat2 = MIN(blat2, MAX(-PI/2,lat1-radius));

			/*
			 * longitude is more complicated, as it needs to wrap
			 * around.
			 *
			 * scale the radius according to our latitude
			 *
			 * remember west is +ve!!!
			 *
			 * Wrap around at 180deg W/E
			 *
			 */

			radius /= cos(lat1);

			lon1 += radius;
			if (lon1 > PI) lon1 -= 2*PI;
			blon1 = MAX(blon1, lon1);
			blon2 = MIN(blon2, lon1);

			lon1 -= 2*radius;
			if (lon1 < -PI) lon1 += 2*PI;
			blon1 = MAX(blon1, lon1);
			blon2 = MIN(blon2, lon1);

		}

		segCount++;
		gets(line);

	}
	segStr[segCount] = 0;
	

	/*
	 * adjust length of strings so that the segment records
	 * start on an even byte boundary
	 *
	 */

	if ( (segCount+1 + strlen(description) + 1) & 1 ) {

		strcat(description,"\n");

	}
	
	/*
	 * add the segment string, name, auth and segment records to the
	 * airspace record
	 *
	 */


	writePtr = (void*)&as->segmentCode;
	memcpy(writePtr, segStr, segCount+1);
	writePtr += segCount+1;
	memcpy(writePtr, description, strlen(description)+1);
	writePtr += strlen(description)+1;

	memcpy(writePtr, segStart, segPtr - segStart);
	writePtr += segPtr - segStart;

	*size = writePtr-(void*)as;

	/*
	 * adjust the longitude limits if the span is > 180 degrees by
	 * swapping lon1 and lon2 around
	 *
	 */

	if (fabs(blon1 - blon2) > PI) {

		double tmp = blon1;

		blon1 = blon2;
		blon2 = tmp;

	}

	*lat1 = RAD_TO_INT16(blat1);
	*lon1 = RAD_TO_INT16(blon1);
	*lat3 = RAD_TO_INT16(blat2);
	*lon3 = RAD_TO_INT16(blon2);
	 
	/*
	 * finally, convert to Palm-Endianess
	 *
	 */

	as->type = PDBPalm16(as->type);
	as->extra = PDBPalm16(as->extra);
	as->lowerAlt = PDBPalm16(as->lowerAlt);
	as->upperAlt = PDBPalm16(as->upperAlt);

	free(description);
	free(segStart);
	free(segStr);

	return as;

}

/*
 * function : AsImportDB
 *
 */

int main(int argc, char **argv) {

	UInt16 j;
	UInt16 numRecords = 0;

	Int16 *index[5];

	PDBType pdb; 

	if (argc < 3 || argc > 4) {

		printf("Usage:  airspace.exe PDB_FILENAME DATABASE_NAME [MESSAGE]\n");
		printf("\nReads FMA formatted input from standard input\n");
		exit(0);

	}

	pdb = PDBNew(argv[2], 'BHMN','FMDB');

	/*
	 * 5 index records
	 *
	 */

	for (j=0;j<lastIdxRec;j++) {

		index[j] = malloc(65536);

	}

	/*
	 * import records
	 *
	 */

	for (j=0;j<32768;j++) {

		Int16 lat1, lon1, lat3, lon3;
		int size;
		AirspaceType *as;

		as = ImportRecord(&lat1, &lon1, &lat3, &lon3, &size);

		if (as == NULL) {
			
			break;

		}

		PDBAddRecord(pdb, (void*)as, size, 0);

		/*
		 * store bounding coordinates and airspace type
		 *
		 */

		index[0][numRecords] = PDBPalm16(lat1);
		index[1][numRecords] = PDBPalm16(lon1);
		index[2][numRecords] = PDBPalm16(lat3);
		index[3][numRecords] = PDBPalm16(lon3);
		index[4][numRecords] = as->type;

		numRecords++;
		if (j == 16383) {

			printf("Error: too many airspace records (max=16384)");
			exit(1);

		}
		free(as);

	}

	/*
	 * store index records
	 *
	 */

	for (j=0;j<5;j++) {

		PDBInsertRecord(pdb, (void*)(index[j]), sizeof(Int16)*numRecords, 0, j);
		free(index[j]);

	}
	
	/*
	 * Set AppInfo block if user has specified a message
	 *
	 */

	if (argc == 4) {

		PDBSetAppInfo(pdb, argv[3], strlen(argv[3])+1);

	}

	PDBWrite(pdb, argv[1]);
	PDBFree(pdb);
	
	printf("Wrote %d airspace records\n", numRecords);
}

