#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <queue>

#define MAXX 640
#define MAXY 480
#define MAXITER 32768

FILE* input; // descriptor for the list of tiles (cannot be stdin)
int  numWorkersThreads = 4; //default number of threads are to be used 

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

std::queue<fractal_param_t> fractalQueue;

pthread_cond_t empty_queue;
pthread_cond_t filled_queue;

pthread_mutex_t workers_queue_access;
pthread_mutex_t empty_queue_mutex;
pthread_mutex_t filled_queue_mutex;

bool startedEOWs = false;


/**
 * @brief Reads a line from the input and sets all attributes of p
 * 
 * @param p out
 * @return int 
 */
int input_params(fractal_param_t& newFractal)
{ 
	int n;
	n = fscanf(input,"%d %d %d %d",&(newFractal.left),&(newFractal.low),&(newFractal.ires),&(newFractal.jres));
	if (n == EOF) return n;

	if (n!=4) {
		perror("fscanf(left,low,ires,jres)");
		exit(-1);
	}
	n = fscanf(input,"%lf %lf %lf %lf",
		 &(newFractal.xmin),&(newFractal.ymin),&(newFractal.xmax),&(newFractal.ymax));
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

void checkForFractalsInQueue(const unsigned int minSizeFractalQueue, const unsigned int myRank){
	if(fractalQueue.size() < minSizeFractalQueue && !startedEOWs){
		pthread_cond_signal(&empty_queue);
		printf("Trabalhadora %ld sinalizou leitora!\n", myRank);

		//Only this thread will be waiting as it got the lock above
		while(pthread_cond_wait(&filled_queue, &filled_queue_mutex) != 0){
			printf("Trabalhadora %ld esta esperando!\n", myRank);
		}
		printf("Trabalhadora %ld viu que foi preenchida!\n", myRank);
	}
}

bool checkForEOW(fractal_param_t &newFractal){
	bool isEOW = newFractal.xmin == 0.0 && newFractal.xmax == 0.0 && newFractal.ymin == 0.0 && newFractal.ymax == 0.0;
	return isEOW;
}

void* readFromFractalQueueAndCalculate(void* rank){
	long myRank = (long) rank;
	printf("Ola da Thread %ld\n", myRank);

	bool eowFlag = false;
	unsigned int minSizeFractalQueue = numWorkersThreads;

	//Gets fractals while it doesnt get a EOW fractal
	while(!eowFlag){

		//Only one worker thread will get pass here
        pthread_mutex_lock(&workers_queue_access);
        
		//Check fractals queue size
		checkForFractalsInQueue(minSizeFractalQueue, myRank)
        
        //fractal(&p);
		fractal_param_t newFractal = fractalQueue.front();
		fractalQueue.pop();
		printf("Trabalhadora %ld consumiu a fila!\n", myRank);

		bool fractalIsEOW = checkForEOW(newFractal);
		if(fractalIsEOW){
			//This thread should not do anything more
			startedEOWs = true;
			printf("Trabalhadora %ld viu que eh um EOW!\n", myRank);
			//Free access to other threads
			pthread_mutex_unlock(&workers_queue_access);
			break;
		}

		//Checks again after consuming a fractal
		checkForFractalsInQueue(minSizeFractalQueue, myRank)

		//Free access to other threads
        pthread_mutex_unlock(&workers_queue_access);

		//If it got here, the fractal should be computed
		fractal(&newFractal);
		
    }

}

void eowFractal(fractal_param_t& myFractal){
	myFractal.left = 0;
	myFractal.low = 0;
	myFractal.ires = 0;
	myFractal.jres = 0;
	myFractal.xmin = 0.0;
	myFractal.xmax = 0.0;
	myFractal.ymin = 0.0;
	myFractal.ymax = 0.0;
}

void* populateFractalQueue(void* rank){
	printf("Ola da Thread 0\n");

	unsigned int maxNumFractalsOnQueue = 4*(numWorkersThreads);
	bool gotToEndOfFile = false;

	while(true){

		//Wait signal to fill queue
        pthread_mutex_lock(&empty_queue_mutex);
		printf("Leitora esta esperando!\n");
        while(pthread_cond_wait(&empty_queue, &empty_queue_mutex) != 0);
		printf("Leitora foi ativada!\n");

		//Insert fractals on queue
		int maxQtFractalToBeInserted =  maxNumFractalsOnQueue - fractalQueue.size();

		printf("Leitora preenchendo %d fractais!\n", maxQtFractalToBeInserted);
		int fractalInsertedCount = 0;
		while(fractalInsertedCount < maxQtFractalToBeInserted && !gotToEndOfFile){

			fractal_param_t newFractal;
			//Fill newFractal and know if reached end of file
			gotToEndOfFile = input_params(newFractal) == EOF;

			//Add the EOW fractals in the queue. May pass the max number of fractals in the queue
			if(gotToEndOfFile){

				printf("Leitora chegou no fim do arquivo. Adicionando EOWS!\n");

				for(int eowNumber = 0; eowNumber < numWorkersThreads; eowNumber++){
					fractal_param_t newEowFractal;
					eowFractal(newEowFractal);
					fractalQueue.push(newEowFractal);
					printf("Adicionou EOW!\n");
				}

			}else{ // Just push the newFractal on queue
				fractalQueue.push(newFractal);
				fractalInsertedCount++;
			}

		}

		//Already did everything it should
        pthread_mutex_unlock(&empty_queue_mutex);
        pthread_cond_signal(&filled_queue);

		printf("Leitora avisou que preencheu!\n");

		//Already added EOW's and should not do anything more
		if(gotToEndOfFile){
			break;
		}
        
    }
	
}

int main ( int argc, char* argv[] )
{
	if ((argc!=2)&&(argc!=3)) {
		fprintf(stderr,"usage %s filename [numWorkersThreads]\n", argv[0] );
		exit(-1);
	} 

	if (argc==3) {
		numWorkersThreads = atoi(argv[2]);
	}

	printf("Num Threads: %d\n", numWorkersThreads);

	if ((input=fopen(argv[1],"r"))==NULL) {
		perror("fdopen");
		exit(-1);
	}

	pthread_t threadsArray[numWorkersThreads+1];

	pthread_mutex_init(&empty_queue_mutex, NULL);
    pthread_mutex_init(&filled_queue_mutex, NULL);
    pthread_mutex_init(&workers_queue_access, NULL);
	pthread_cond_init(&empty_queue, NULL);
    pthread_cond_init(&filled_queue, NULL);

	for(long threadRank = 0; threadRank<numWorkersThreads; threadRank ++){
		if(threadRank == 0){
			pthread_create(&threadsArray[threadRank], NULL, populateFractalQueue, (void*) threadRank);
		}else{
			pthread_create(&threadsArray[threadRank], NULL, readFromFractalQueueAndCalculate, (void*) threadRank);
		}
	}

	for(long threadRank = 0; threadRank < numWorkersThreads; threadRank ++){
		pthread_join(threadsArray[threadRank], NULL);
	}

	pthread_mutex_destroy(&empty_queue_mutex);
    pthread_mutex_destroy(&filled_queue_mutex);
    pthread_mutex_destroy(&workers_queue_access);
    pthread_cond_destroy(&empty_queue);
    pthread_cond_destroy(&filled_queue);

	return 0;
}

