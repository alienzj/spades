/*
 * center.cpp
 *
 *  Created on: 11.05.2011
 *      Author: snikolenko
 */

#include<cassert>
#include<cmath>
#include<string>
#include<iostream>
#include<sstream>
#include<fstream>
#include<cstdlib>
#include<vector>
#include<map>
#include<list>
#include<queue>
#include<cstdarg>
#include<algorithm>
#include<stdexcept>

#include "defs.hpp"
#include "hammerread.hpp"
#include "mathfunctions.hpp"

using namespace std;
ostringstream oMsg;
string sbuf;

class ThreadStatistics {
  public:
	int thread;
	int old_clusters;
	int new_clusters;
	int old_singletons;
	int new_singletons;
	
	ThreadStatistics(int i) : thread(i), old_clusters(0), new_clusters(0),
		old_singletons(0), new_singletons(0) {}
};

class BadConversion : public std::runtime_error {
 public:
   BadConversion(std::string const& s)
     : std::runtime_error(s)
     { }
};

inline double convertToInt(std::string const& s) {
   istringstream i(s);
   int x;
   if (!(i >> x))
     throw BadConversion("convertToInt(\"" + s + "\")");
   return x;
}

double errorRate = 0.01;
double ambigConst = 1;
int restartCountDefault = 5;
int nthreads;
string ufFilename;
double threshold;

// ambiguity threshold for not making a decision at this iteration
double ambigThreshold = 1;

double calcMultCoef(vector<int> & distances, vector<HammerRead> & kmers) {
	int kmersize = kmers[0].seq.size();
	double prob = 0;
	double theta;
	for (size_t i = 0; i < kmers.size(); i++) {
		theta = kmers[i].count * -((kmersize - distances[i]) * log(1 - errorRate) + distances[i] * log(errorRate));
		prob = prob + theta;
	}
	return prob;
}


/**
  * SIN
  * find consensus with mask
  * @param mask is a vector of integers of the same size as the block
  * @param maskVal is the integer that we use
  */
string find_consensus_with_mask(vector<HammerRead> & block, const vector<int> & mask, int maskVal) {
	string c;
	for (size_t i = 0; i < block[0].seq.length(); i++) {
		int scores[4] = {0,0,0,0};
		for (size_t j = 0; j < block.size(); j++) {
			if (mask[j] == maskVal)
				scores[nt2num(block[j].seq.at(i))] += block[j].count;
		}
		c.push_back(num2nt(argmax(scores, 4)));
	}
	return c;
}


string find_consensus(vector<HammerRead> & block) {
	string c;
	for (size_t i = 0; i < block[0].seq.length(); i++) {
		int scores[4] = {0,0,0,0};
		for (size_t j = 0; j < block.size(); j++) {
			scores[nt2num(block[j].seq.at(i))]+=block[j].count;
		}
		c.push_back(num2nt(argmax(scores, 4)));
	}
	return c;
}



/**
  * @return total log-likelihood of this particular clustering
  */
double clusterLogLikelihood(const vector<HammerRead> & block, const vector<HammerRead> & centers, const vector<int> & indices) {
	if (block.size() == 0) return 0.0;
	assert(block.size() == indices.size());
	assert(centers.size() > 0);
	
	// if there is only one center, there are no beta coefficients
	if (centers.size() == 1) {
		int dist = 0;
		for (size_t i=0; i<block.size(); ++i) {
			dist += hamdist(block[i].seq, centers[indices[i]].seq, ANY_STRAND);
		}
		return ( lMultinomial(block) + log(errorRate) * dist + log(1-errorRate) * (block[0].seq.size() * block.size() - dist) );
	}
	
	// compute sufficient statistics
	vector<int> count(centers.size(), 0);		// how many kmers in cluster i
	vector<int> totaldist(centers.size(), 0);	// total distance from kmers of cluster i to its center
	for (size_t i=0; i<block.size(); ++i) {
		count[indices[i]]+=block[i].count;
		totaldist[indices[i]] += hamdist(block[i].seq, centers[indices[i]].seq, ANY_STRAND);
	}
	
	// sum up the likelihood
	double res = lBetaPlusOne(count);   // 1/B(count)
	res += lMultinomial(centers); 		// {sum(centers.count) \choose centers.count}
	for (size_t i=0; i<centers.size(); ++i) {
		res += lMultinomialWithMask(block, indices, i) + 
			   log(errorRate) * totaldist[i] + log(1-errorRate) * (block[0].seq.size() * count[i] - totaldist[i]);
	}
	return res;
}


/**
  * perform l-means clustering on the set of k-mers with initial centers being the l most frequent k-mers here
  * @param indices fill array centers with cluster centers; centers[k].count shows how many different kmers are in this cluster (used later)
  * @param centers fill array indices with ints from 0 to l that denote which kmers belong where
  * @return the resulting likelihood of this clustering
  */
double lMeansClustering(int l, vector< vector<int> > & distances, vector<HammerRead> & kmers, vector<int> & indices, vector<HammerRead> & centers) {
	centers.resize(l); // there are l centers
	// if l==1 then clustering is trivial
	if (l == 1) {
		centers[0].seq = find_consensus(kmers);
		centers[0].count = kmers.size();
		for (size_t i=0; i < kmers.size(); ++i) indices[i] = 0;
		return clusterLogLikelihood(kmers, centers, indices);
	}
	
	//cout << "Running " << l << "-means clustering.\n";
	
	int restartCount = 1;
	
	double bestLikelihood = -1000000.0;
	double curLikelihood = -1000000.0;
	vector<HammerRead> bestCenters;
	vector<int> bestIndices(kmers.size());
	
	for (int restart = 0; restart < restartCount; ++restart) {

	// fill in initial approximations
	for (int j=0; j < l; ++j) centers[j].count = 0;
	for (size_t i=0; i < kmers.size(); ++i) {
		for (int j=0; j < l; ++j) {
			if (kmers[i].count > centers[j].count) {
				for (int s=j; s<l-1; ++s) {
					centers[s+1].seq = centers[s].seq;
					centers[s+1].count = centers[s].count;
				}
				centers[j].seq = kmers[i].seq;
				centers[j].count = kmers[i].count;
				break;
			}
		}
	}

	// TODO: make random restarts better!!! they don't really work now
	if (restart > 0) { // introduce random noise
		vector<bool> good(kmers.size(), false);
		for (size_t i=0; i < kmers.size(); ++i) {
			for (int j=0; j < l; ++j) {
				if (kmers[i].count >= centers[j].count) {
					good[i] = true; break;
				}
			}
		}
		for (int k=0; k < l/2 + 1; ++k) {
			int newNo = rand() % (kmers.size() - l - k);
			int indexOld = (rand() % (l-1)) + 1;
			int indexNew = kmers.size()-1;
			for (size_t i=0; i < kmers.size(); ++i) {
				if (!good[i]) --newNo;
				if (newNo < 0) { indexNew = i; good[indexNew] = true; break; }
			}
			centers[indexOld].seq = kmers[indexNew].seq;
		}
	}
	
	// auxiliary variables
	vector<int> dists(l);
	vector<bool> changedCenter(l);
	
	// main loop
	bool changed = true;
	while (changed) {
		// fill everything with zeros
		changed = false;
		fill(changedCenter.begin(), changedCenter.end(), false);
		for (int j=0; j < l; ++j) centers[j].count = 0;
		
		// E step: find which clusters we belong to
		for (size_t i=0; i < kmers.size(); ++i) {
			for (int j=0; j < l; ++j)
				dists[j] = hamdist(kmers[i].seq, centers[j].seq, ANY_STRAND);
			int newInd = argmin(dists);
			if (indices[i] != newInd) {
				changed = true;
				changedCenter[indices[i]] = true;
				changedCenter[newInd] = true;
				indices[i] = newInd;
			}
			++centers[indices[i]].count;
		}
		
		// M step: find new cluster centers
		for (int j=0; j < l; ++j) {
			if (!changedCenter[j]) continue; // nothing has changed
			centers[j].seq = find_consensus_with_mask(kmers, indices, j);
		}
	}

	// last M step
	for (int j=0; j < l; ++j) {
		centers[j].seq = find_consensus_with_mask(kmers, indices, j);
	}
	
	curLikelihood = clusterLogLikelihood(kmers, centers, indices);
	if (restartCount > 1 && curLikelihood > bestLikelihood) {
		bestLikelihood = curLikelihood;
		bestCenters = centers; bestIndices = indices;
	}
	
	} // end restarts
	
	if (restartCount > 1) {
		centers = bestCenters; indices = bestIndices; curLikelihood = bestLikelihood;
	}
	return curLikelihood;
}


/**
  * SIN
  * new version of process_block
  * @param newBlockNum current number of a new cluster; incremented inside
  * @return new value of newBlockNum
  */
int process_block_SIN(vector<HammerRead> & block, int blockNum, double threshold, ofstream & outf, int newBlockNum, ThreadStatistics *tstat) {
	int origBlockSize = block.size();
	if (origBlockSize == 0) return newBlockNum;
	
	tstat->old_clusters++; // we've got a cluster
	if (origBlockSize == 1) tstat->old_singletons++; // a singleton

	vector<double> multiCoef(origBlockSize,1000000);//add one for the consensus
	vector<int> distance(origBlockSize, 0);
	vector< vector<int> > distances(origBlockSize, distance);
	string newkmer;
	string reason = "noreason";

	//Calculate distance matrix
	for (size_t i = 0; i < block.size(); i++) {
		distances[i][i] = 0;
		for (size_t j = i + 1; j < block.size(); j++) {
			distances[i][j] = hamdist(block[i].seq, block[j].seq, ANY_STRAND);
			distances[j][i] = distances[i][j];
		}
	}

	/*for (size_t i = 0; i < block.size(); i++) {
		for (size_t j = 0; j < block.size(); j++) {
			cout << distances[i][j] << " ";
		}
		cout << "\n";
	}*/

	// Multinomial coefficients -- why? TODO: remove
	for (int i = 0; i < origBlockSize; i++) multiCoef[i] = calcMultCoef(distances[i], block);

	vector<int> indices(origBlockSize);
	double bestLikelihood = -1000000;
	vector<HammerRead> bestCenters;
	vector<int> bestIndices(block.size());

	for (int l = 1; l <= origBlockSize; ++l) {
		vector<HammerRead> centers(l);
		double curLikelihood = lMeansClustering(l, distances, block, indices, centers);
		/*if (centers.size() > 1) {
			cout << "Centers:\n"; for (int i=0; i<l; ++i) cout << "  " << centers[i].seq << "\n";
			cout << "Indices: "; for (size_t i=0; i<block.size(); ++i) cout << indices[i]; cout << "\n";
			cout.width(6); cout.precision(2);
			printf("Likelihood: %f\n", curLikelihood);
		} else if (block.size() > 1) {
			printf("Single-center likelihood: %f\n", curLikelihood);
		}*/
		if (curLikelihood <= bestLikelihood) {
			//cout << "We've stopped improving. Exiting.\n";
			break;
		}
		bestLikelihood = curLikelihood;
		bestCenters = centers; bestIndices = indices;
		//if (centers.size() > 1) cout << "\n";
	}
	
	for (size_t k=0; k<bestCenters.size(); ++k) {
		if (bestCenters[k].count == 0) {
			// cout << "Cluster " << k << " has no elements. Strange.\n";
			continue; // superfluous cluster with zero elements
		}
		if (bestCenters[k].count == 1) { 
			continue; //singleton
			// if (block.size() > 1) cout << "Cluster " << k << " is a singleton. ";
			/*tstat->new_singletons++; // a singleton among the new clusters
			for (int i = 0; i < origBlockSize; i++) {
				if (indices[i] == (int)k) {
					if (block[i].freq >= threshold) { //keep reads
						//if (block.size() > 1)  cout << "Frequency " << block[i].freq << " is good.\n";
						newkmer =  block[i].seq;
						reason =  "goodSingleton";
					} else {
						//if (block.size() > 1)  cout << "Frequency " << block[i].freq << " is bad.\n";
						newkmer = "removed";
						reason =  "badSingleton";
					}
					outf << newBlockNum << "\t" << block[i].seq << "\t" << block[i].count << "\t" << block[i].freq << "\t" << block.size();
					outf <<  "\t" << multiCoef[i] << "\t" << reason << "\t";
					outf << hamdist(block[i].seq, bestCenters[k].seq, ANY_STRAND); 
					outf << endl;
					break;
				}
			}
			++newBlockNum;*/
		} else { // there are several kmers in this cluster
			// cout << "Cluster " << k << " has " << bestCenters[k].count << " different kmers.\n";
			bool centerInCluster = false;
			for (int i = 0; i < origBlockSize; i++) {
				if (bestIndices[i] == (int)k) {
					int dist = hamdist(block[i].seq, bestCenters[k].seq, ANY_STRAND);
					if (dist == 0) {
						centerInCluster = true;
						reason = "center";
					} else {
						reason = "change";
					}
					outf << newBlockNum << "\t" << block[i].seq << "\t" << block[i].count << "\t" << block[i].freq << "\t" << block.size();
					outf <<  "\t" << multiCoef[i] << "\t" << reason << "\t" << dist << endl;
				}
			}
			if (!centerInCluster) {
				outf << newBlockNum << "\t" << bestCenters[k].seq << "\t" << 0 << "\t" << 0.0 << "\t" << block.size();
				outf <<  "\t" << 0.0 << "\t" << "center" << "\t" << 0 << endl;
			}
			++newBlockNum;
		}
	}
	outf << endl;
	// cout << "\n";
	return newBlockNum;
}

void * onethread(void * params) {
	ThreadStatistics *tstat = (ThreadStatistics *)(params);
	int thread = tstat->thread;
	cout << thread << "\n";

	ifstream inf;
	open_file(inf, ufFilename);
	ofstream outf;
	open_file(outf, "reads.uf.corr." + make_string(thread));

	vector<HammerRead> block;
	int counter=0;
	vector<string> row;
	int curBlockNum = 0;
	int lastBlockNum = -1;
	int newBlockNum = 0;
	while (get_row_whitespace(inf, row)) {
		if (row.size() < 1 || row[0] != "ITEM") {
			//outf << row << endl;
			continue;
		}
		if (atoi(row[1].c_str()) % nthreads != thread) continue;
		if (++counter % 1000000 == 0) cout << "Processed " << add_commas(counter) << ".\n";
		HammerRead cur;
		curBlockNum = convertToInt(row[1]);
		cur.id = row[2];
		cur.seq = row[3];
		cur.count = atoi(row[4].c_str());
		cur.freq = atof(row[5].c_str());
		if (lastBlockNum == curBlockNum) { //add to current reads
			block.push_back(cur);
		} else {
			newBlockNum = process_block_SIN(block, lastBlockNum, threshold, outf, newBlockNum, tstat);
			block.clear();
			block.push_back(cur);
		}
		lastBlockNum = curBlockNum;
	}
	if (block.size() > 0)
		newBlockNum = process_block_SIN(block, lastBlockNum, threshold, outf, newBlockNum, tstat);
	tstat->new_clusters = newBlockNum;
	
	inf.close();
	outf.close();
	pthread_exit(NULL);
}

int main(int argc, char * argv[]) {
	ufFilename = argv[1];
	threshold = atof(argv[2]);
	nthreads = atoi(argv[3]);
	cout << "Starting. Error rate = " << errorRate << ". Threshold = " << threshold << ". " << nthreads << " threads.\n";
	flush(cout);

	//start threads
	vector<pthread_t> thread(nthreads);
	vector<ThreadStatistics *> params(nthreads);
	for (int i = 0; i < nthreads; i++) {
		params[i] = new ThreadStatistics(i);
		pthread_create(&thread[i], NULL, onethread, (void *)params[i]);
	}
	for (int i = 0; i < nthreads; i++) {
		pthread_join(thread[i], NULL);
	}

	cout << "Thread statistics:\n";
	for (int i = 0; i < nthreads; i++) {
		cout << "  thread " << params[i]->thread << ": split " << params[i]->old_clusters << " to " << params[i]->new_clusters;
		cout << "; " << params[i]->old_singletons << " became " << params[i]->new_singletons << "\n";
	}
	
	ThreadStatistics *total = new ThreadStatistics(-1);
	for (int i = 0; i < nthreads; i++) {
		total->old_clusters += params[i]->old_clusters;
		total->new_clusters += params[i]->new_clusters;
		total->old_singletons += params[i]->old_singletons;
		total->new_singletons += params[i]->new_singletons;
		
		delete params[i];
	}
	
	cout << "\nTotal statistics:\n";
	cout << "  " << total->old_clusters << " clusters before.\n";
	cout << "  " << total->new_clusters << " clusters after.\n";
	cout << "  " << total->old_singletons << " singletons before.\n";
	cout << "  " << total->new_singletons << " singletons after.\n";
	
	params.clear(); delete total;
	
	return 0;
}

