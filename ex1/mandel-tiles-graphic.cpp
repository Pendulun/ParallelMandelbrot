#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#define MAXX 640
#define MAXY 480
#define MAXITER 32768

FILE* input; // descriptor for the list of tiles (cannot be stdin)
int  numThreads = 4; //default number of threads are to be used 
 
/**
 * @brief Params for each call to the fractal function
 * 
 */
typedef struct {
	int left; int low;  // lower left corner in the screen
	int ires; int jres; // resolution in pixels of the area to compute
	double xmin; double ymin;   // lower left corner in domain (x,y)
	double xmax; double ymax;   // upper right corner in domain (x,y)
} fractal_param_t;

/**
 * @brief Reads a line from the input and sets all attributes of p
 * 
 * @param p out
 * @return int 
 */
int input_params( fractal_param_t* p )
{ 
	int n;
	n = fscanf(input,"%d %d %d %d",&(p->left),&(p->low),&(p->ires),&(p->jres));
	if (n == EOF) return n;

	if (n!=4) {
		perror("fscanf(left,low,ires,jres)");
		exit(-1);
	}
	n = fscanf(input,"%lf %lf %lf %lf",
		 &(p->xmin),&(p->ymin),&(p->xmax),&(p->ymax));
	if (n!=4) {
		perror("scanf(xmin,ymin,xmax,ymax)");
		exit(-1);
	}
	return 8;

}

/**
 * @brief Function to calculate mandelbrot set
 * 
 * @param p 
 */
void fractal( fractal_param_t* p )
{
	double dx, dy;
	int i, j, k;
	double x, y, u, v, u2, v2;

	dx = ( p->xmax - p->xmin ) / p->ires;
	dy = ( p->ymax - p->ymin ) / p->jres;
	
	// scanning every point in that rectangular area.
	// Each point represents a Complex number (x + yi).
	// Iterate that complex number
	for (j = 0; j < p->jres; j++) {
		for (i = 0; i <= p->ires; i++) {
			x = i * dx + p->xmin; // c_real
			u = u2 = 0; // z_real
			y = j * dy + p->ymin; // c_imaginary
			v = v2 = 0; // z_imaginary

			// Calculate whether c(c_real + c_imaginary) belongs
			// to the Mandelbrot set or not and draw a pixel
			// at coordinates (i, j) accordingly
			// If you reach the Maximum number of iterations
			// and If the distance from the origin is
			// greater than 2 exit the loop
			for (k=0; (k < MAXITER) && ((u2+v2) < 4); ++k) {
				// Calculate Mandelbrot function

				v = 2 * u * v + y;
				u  = u2 - v2 + x;
				u2 = u * u;
				v2 = v * v;
			}
		}
	}
}


int main ( int argc, char* argv[] )
{
	fractal_param_t p;

	if ((argc!=2)&&(argc!=3)) {
		fprintf(stderr,"usage %s filename [numThreads]\n", argv[0] );
		exit(-1);
	} 

	if (argc==3) {
		numThreads = atoi(argv[2]);
	}

	if ((input=fopen(argv[1],"r"))==NULL) {
		perror("fdopen");
		exit(-1);
	}

	int count = 0;
	while (input_params(&p)!=EOF) {
		fractal(&p);
		count++;
	}

	pthread_t* threadsArray;
	threadsArray = new pthread_t[numThreads];

	delete[] threadsArray;
	return 0;
}

