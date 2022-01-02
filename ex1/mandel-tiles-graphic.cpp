#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <queue>
#include <vector>
#include <list>
#include <chrono>
#include <iterator>
#include <math.h>

#define MAXX 640
#define MAXY 480
#define MAXITER 32768

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

//Statistics struct
typedef struct{
	unsigned int totalFractalsComputed;
	unsigned int emptyQueueCount;
	float totalTimeComputed;
	std::list<std::chrono::duration<double, std::milli>> execTimesPerFractalComputed;
} thread_exec_stats;

//Queue where all worker threads will access
std::queue<fractal_param_t> fractalQueue;

//Vector that contains statistics for every worker thread
std::vector<thread_exec_stats> executionStats;

//Indicates if the queue is empty
pthread_cond_t empty_queue;
pthread_mutex_t empty_queue_mutex;

//Indicates if the queue was filled by the file reading thread
pthread_cond_t filled_queue;
pthread_mutex_t filled_queue_mutex;

//A mutex to the fractal queue for worker processes
pthread_mutex_t workers_queue_access;

FILE* input; // descriptor for the list of tiles (cannot be stdin)

int  numWorkersThreads = 4; //default number of threads are to be used 
unsigned int minSizeFractalQueue = 0; //It must be equal to the numWorkersThreads

//Flag that indicates if an EOW fractal has been read by some worker Thread
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

/**
 * @brief Checks if the fractals queue has the minimum amount of fractals
 * 
 * @param minSizeFractalQueue 
 * @param myRank 
 */
void checkForFractalsInQueue(const unsigned int minSizeFractalQueue, const unsigned int myRank){
	//If startedEOWs, than we shouldn't care if the queue has less then the minimum amount of fractals
	if(fractalQueue.size() < minSizeFractalQueue && !startedEOWs){

		//For statistics purposes
		if(fractalQueue.empty()){
			executionStats.at(myRank).emptyQueueCount += 1;
		}
		//Signal the file reader thread that it should fill the queue
		pthread_cond_signal(&empty_queue);
		
		//Waits for the file reader thread to signal that it filled the queue
		//Only this thread will be waiting as it got the queue lock
		while(pthread_cond_wait(&filled_queue, &filled_queue_mutex) != 0){
			//Just waits
		}

	}

	//Free to access the queue
}

/**
 * @brief Checks if the fractal represents an EOW fractal
 * 
 * @param newFractal 
 * @return true 
 * @return false 
 */
bool isFractalEOW(fractal_param_t &newFractal){
	bool isEOW = newFractal.xmin == 0.0 && newFractal.xmax == 0.0 && newFractal.ymin == 0.0 && newFractal.ymax == 0.0;
	return isEOW;
}

/**
 * @brief Sets the initial values for the thread's stats
 * 
 * @param threadRank 
 */
void clearThreadStats(unsigned int threadRank){
	executionStats.at(threadRank).totalFractalsComputed = 0;
	executionStats.at(threadRank).emptyQueueCount = 0;
	executionStats.at(threadRank).execTimesPerFractalComputed.clear();
	executionStats.at(threadRank).totalTimeComputed	= 0.0;
}

/**
 * @brief Worker thread main function for consuming the fractal queue until it gets a EOW fractal
 * 
 * @param rank 
 * @return void* 
 */
void* readFromFractalQueueAndCalculate(void* rankTotal){
	long myRank = (long) rankTotal;
	long workerRank = myRank - 1;

	clearThreadStats((unsigned int) workerRank);

	//Indicates if this thread popped an EOW fractal from the fractal queue
	bool eowFlag = false;

	//Gets fractals while it doesn't get an EOW fractal
	while(!eowFlag){

		//Only one worker thread will get pass here
        pthread_mutex_lock(&workers_queue_access);
        
		checkForFractalsInQueue(minSizeFractalQueue, workerRank);
        
		//Gets a new fractal from the queue
		fractal_param_t newFractal = fractalQueue.front();
		fractalQueue.pop();

		if(isFractalEOW(newFractal)){

			//Indicates to other threads that the EOW fractals have begun appearing in the queue
			startedEOWs = true;

			//Free access to other threads
			pthread_mutex_unlock(&workers_queue_access);

			//Dont do anything more
			eowFlag = true;
			break;
		}

		//Checks again after consuming a fractal
		checkForFractalsInQueue(minSizeFractalQueue, workerRank);

		//Free the access to the fractal queue to other worker threads
        pthread_mutex_unlock(&workers_queue_access);

		//If it got here, the fractal should be computed
		auto initTime = std::chrono::high_resolution_clock::now();
		fractal(&newFractal);
		auto endTime = std::chrono::high_resolution_clock::now();
		
		//Collects statistics for this thread
		//As each thread has its own thread_exec_stats, this is thread-safe
		auto thisExecTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - initTime);
		executionStats.at(workerRank).totalFractalsComputed += 1;
		executionStats.at(workerRank).totalTimeComputed += thisExecTime.count();
		executionStats.at(workerRank).execTimesPerFractalComputed.push_back(thisExecTime);
    }

}

/**
 * @brief Turns the fractal into an EOW fractal
 * 
 * @param myFractal 
 */
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

/**
 * @brief Main function of the thread that reads the input files and put fractal on the fractals queue
 * 
 * @param rank 
 * @return void* 
 */
void* populateFractalQueue(void* rank){
	unsigned int maxNumFractalsOnQueue = 4*(numWorkersThreads);
	bool gotToEndOfFile = false;

	//Main file read and queue fill loop
	while(!gotToEndOfFile){

		//Wait signal to fill queue
        pthread_mutex_lock(&empty_queue_mutex);
        while(pthread_cond_wait(&empty_queue, &empty_queue_mutex) != 0){
			//Just waits
		}

		//This thread has been awaken!

		//Insert fractals on queue
		int maxQtFractalToBeInserted =  maxNumFractalsOnQueue - fractalQueue.size();
		int fractalInsertedCount = 0;
		bool shouldReadFileAndPushQueue = fractalInsertedCount < maxQtFractalToBeInserted && !gotToEndOfFile;
		while(shouldReadFileAndPushQueue){
			//Creates and fill newFractal and know if reached end of file
			fractal_param_t newFractal;
			gotToEndOfFile = input_params(newFractal) == EOF;

			if(gotToEndOfFile){
				//Add the EOW fractals in the queue.
				// May pass the max number of fractals in the queue. It's for a greater good

				for(int eowNumber = 0; eowNumber < numWorkersThreads; eowNumber++){
					fractal_param_t newEowFractal;
					eowFractal(newEowFractal);
					fractalQueue.push(newEowFractal);
				}

			}else{ // Just push the newFractal on queue
				fractalQueue.push(newFractal);
				fractalInsertedCount++;
			}

			shouldReadFileAndPushQueue = fractalInsertedCount < maxQtFractalToBeInserted && !gotToEndOfFile;
		}

		//Already did everything it should
        pthread_mutex_unlock(&empty_queue_mutex);

		//Signal the only worker thread waiting that the queue has been filled
        pthread_cond_signal(&filled_queue);
    }
}

/**
 * @brief Computes and prints execution statistics
 * 
 */
void computeAndPrintStatistics(){
	//Get total Stats
	unsigned int totalEmptyQueueCount = 0;
	unsigned int totalFractalsComputed = 0;
	double totalTimeExec = 0.0;

	std::list<std::chrono::duration<double, std::milli>>::iterator execTimeIterator;
	for(unsigned int statsIndex = 0; statsIndex < executionStats.size(); statsIndex ++){

		thread_exec_stats threadStats = executionStats.at(statsIndex);
		totalEmptyQueueCount += threadStats.emptyQueueCount;
		totalFractalsComputed += threadStats.totalFractalsComputed;
		totalTimeExec += threadStats.totalTimeComputed;
	}

	//Calculates Mean Stats
	float meanFractalsComputed = 0.0;
	double meanTimeComputingFractals = 0.0;

	meanTimeComputingFractals = totalTimeExec/totalFractalsComputed;
	meanFractalsComputed = totalFractalsComputed/numWorkersThreads;

	//Computes Variation
	float sumSquaredMeanErrorExecTime = 0.0;
	float sumSquaredMeanErrorFractalsComputed = 0.0;
	
	for(unsigned int statsIndex = 0; statsIndex < executionStats.size(); statsIndex ++){
		
		thread_exec_stats threadStats = executionStats.at(statsIndex);
		sumSquaredMeanErrorFractalsComputed += pow(meanFractalsComputed - threadStats.totalFractalsComputed, 2);

		for(execTimeIterator = threadStats.execTimesPerFractalComputed.begin();
								execTimeIterator != threadStats.execTimesPerFractalComputed.end();
								execTimeIterator++){
									double meanError = meanTimeComputingFractals-(*execTimeIterator).count();
									sumSquaredMeanErrorExecTime += pow(meanError, 2);
								
								} 
	}
	float variationTimeComputingFractals = sumSquaredMeanErrorExecTime/(totalFractalsComputed-1);
	float variationFractalsComputed = sumSquaredMeanErrorFractalsComputed/(totalFractalsComputed-1);

	//Computes Standard Error
	float standardDeviationTimeComputingFractals = pow(variationTimeComputingFractals, 0.5);
	float standardDeviationFractalsComputed = pow(variationFractalsComputed, 0.5);

	printf("Tarefas: total = %u; media por trabalhador = %f(%f)\n", totalFractalsComputed, meanFractalsComputed,
																				 standardDeviationFractalsComputed);
	printf("Tempo medio por tarefa: %.6f (%.6f) ms\n", meanTimeComputingFractals, standardDeviationTimeComputingFractals);
	printf("Fila estava vazia: %u vezes\n", totalEmptyQueueCount);
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

	//Resize the statistics vector to fit each worker thread
	executionStats.resize(numWorkersThreads);
	minSizeFractalQueue = numWorkersThreads;

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


	//Starts each thread to corresponding function
	for(long threadRank = 0; threadRank<numWorkersThreads+1; threadRank ++){
		if(threadRank == 0){
			pthread_create(&threadsArray[threadRank], NULL, populateFractalQueue, (void*) threadRank);
		}else{
			pthread_create(&threadsArray[threadRank], NULL, readFromFractalQueueAndCalculate, (void*) threadRank);
		}
	}


	for(long threadRank = 0; threadRank < numWorkersThreads+1; threadRank ++){
		pthread_join(threadsArray[threadRank], NULL);
	}

	pthread_mutex_destroy(&empty_queue_mutex);
    pthread_mutex_destroy(&filled_queue_mutex);
    pthread_mutex_destroy(&workers_queue_access);
    pthread_cond_destroy(&empty_queue);
    pthread_cond_destroy(&filled_queue);

	computeAndPrintStatistics();

	return 0;
}