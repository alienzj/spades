#ifndef PARAMETERS_H_
#define PARAMETERS_H_

#include <fstream>
#include <sstream>
#include "ireadstream.hpp"

/**
 * K-mer size
 */
#define K 25

// ===== Directories ===== //
#define INPUT_DIRECTORY std::string("./data/input/cropped/")
#define OUTPUT_DIRECTORY std::string("./data/abruijn/")

// ===== Input data ===== //
//#define INPUT_DATA_SET std::string("MG1655-K12_emul")
//#define INPUT_DATA_SET std::string("s_6_")
#define INPUT_DATA_SET std::string("s_6.first10000_")
/**
 * How many reads we process
 */
//#define CUT 400

// ===== Visualization ===== //
/**
 * Define OUTPUT_PAIRED if you want GraphViz to glue together complementary vertices
 */
#define OUTPUT_PAIRED
/**
 * How many nucleotides are put in the vertex label in GraphViz
 */
//#define LABEL 5

// ===== Naming conventions ===== //
#define INPUT_FILES INPUT_DIRECTORY + INPUT_DATA_SET + "1.fastq.gz", INPUT_DIRECTORY + INPUT_DATA_SET + "2.fastq.gz"
#define OUTPUT_FILE INPUT_DATA_SET + toString(K) + OUTPUT_FILE_SUFFIX1 + OUTPUT_FILE_SUFFIX2
#ifdef CUT
	#define OUTPUT_FILE_SUFFIX1 "_" + toString(CUT)
#endif
#ifndef CUT
	#define OUTPUT_FILE_SUFFIX1 ""
#endif
#ifdef OUTPUT_PAIRED
	#define OUTPUT_FILE_SUFFIX2 ""
#endif
#ifndef OUTPUT_PAIRED
	#define OUTPUT_FILE_SUFFIX2 "_s"
#endif
#define OUTPUT_FILES OUTPUT_DIRECTORY + OUTPUT_FILE

#define HTAKE 1

// ===== Debug parameters ===== //
#define VERBOSE(n, message) if ((n & 8191) == 8191) INFO(n << message)

#endif /* PARAMETERS_H_ */
