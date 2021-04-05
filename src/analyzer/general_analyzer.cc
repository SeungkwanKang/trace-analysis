// MIT License

// Copyright (c) 2021 KangSK-KAIST

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "general_analyzer.hh"

/**
 * @brief Counts number of each occurences; indep/single/multi
 *
 * @param vTraceData (pointer) vector of trace data
 * @param mCentric (pointer) map of read or write centric
 * @param isRead whether to analyze read or write traces
 * @param indep (pointer) number of independent traces
 * @param depShort (pointer) number of single dependent traces
 * @param depLong (pointer) number of multiple dependent traces
 *
 * @note Independent traces are read/write traces that are reading/writing from
 * addresses never written/read by others.
 * Single dependent traces are read/write traces thar are reading/writing from
 * addresses (written by one)/(read once) by others.
 * Multiple dependent traces are read/write traces thar are reading/writing from
 * addresses (written by multiple)/(read multiple times) multiply by others,
 * where written multiply means it is reading from a segmented range of address,
 * not meaning hotspot.
 */
static void analyzeDependTypes(std::vector<TraceData>* vTraceData,
                               std::map<id_t, std::set<id_t>>* mCentric,
                               bool isRead, int32_t* indep, int32_t* depShort,
                               int32_t* depLong) {
  for (auto trace : (*vTraceData)) {
    if (trace.isRead == isRead) {
      if (mCentric->count(trace.id)) {
        // Exists a corresponding write
        if ((*mCentric)[trace.id].size() > 1)
          (*depLong)++;
        else
          (*depShort)++;
      } else
        (*indep)++;
    }
  }
}

/**
 * @brief Counts all hot writes in page-level
 *
 * @param vTraceData (pointer) vector of traces
 * @param mWriteCentric (pointer) write centric map
 *
 * @note Hot writes means the data written by the write was read by other reads.
 * The function iterates through all hot writes and the pages they wrote, and
 * count the number of occurances of which the data was read by another request.
 * This is done in a page-specific manner, which hereby results in the number of
 * reads each pages experience before overwritten.
 */
static void analyzeHotWrite(std::vector<TraceData>* vTraceData,
                            std::map<id_t, std::set<id_t>>* mWriteCentric) {
  std::vector<uint32_t> readCountsTotal;
  for (auto write : *mWriteCentric) {
    // Iterate for all writes that are read at least once
    // Read basic parameters
    int64_t pageStart = (*vTraceData)[write.first].sLBA / PAGE_SIZE;
    int64_t pageEnd =
        ((*vTraceData)[write.first].sLBA + (*vTraceData)[write.first].nLB) /
        PAGE_SIZE;
    int64_t pageNum = pageEnd - pageStart + 1;

    // Simulate a small segemnt of memory as page array
    int32_t* readCounts = new int32_t[pageNum];
    for (int i = 0; i < pageNum; i++) {
      readCounts[i] = 0;
    }

    // Iterate for all reads that read the pages
    for (auto read : write.second) {
      int64_t readStartReal = (*vTraceData)[read].sLBA / PAGE_SIZE;
      int64_t readEndReal =
          ((*vTraceData)[read].sLBA + (*vTraceData)[read].nLB) / PAGE_SIZE;
      // Read might start earilier, end later than write
      int64_t readStartOverlap = std::max(pageStart, readStartReal);
      int64_t readEndOverlap = std::min(pageEnd, readEndReal);
      // Calculate offset of read request in page count
      int64_t readOffset = readStartOverlap - pageStart;
      int64_t readNum = readEndOverlap - readStartOverlap + 1;
      // Iterate for overlapping pages
      for (int i = 0; i < readNum; i++) {
        readCounts[readOffset + i]++;
      }
    }
    // Finished simulation; append to total vector
    for (int i = 0; i < pageNum; i++) {
      readCountsTotal.push_back(readCounts[i]);
    }
    delete[] readCounts;
  }
  // Count each numbers for better reading
  std::map<int32_t, int64_t> countDuplicate;
  std::for_each(readCountsTotal.begin(), readCountsTotal.end(),
                [&countDuplicate](int val) { countDuplicate[val]++; });

  // Finished all analysis; print data
  std::cout << "[HotWrite]" << std::endl;
  for (auto hot : countDuplicate) {
    std::cout << hot.first << "\t";
  }
  std::cout << std::endl;
  for (auto hot : countDuplicate) {
    std::cout << hot.second << "\t";
  }
  std::cout << std::endl;
}

void analyze(std::vector<TraceData>* vTraceData, int64_t pageNum,
             std::map<id_t, std::set<id_t>>* mReadCentric,
             std::map<id_t, std::set<id_t>>* mWriteCentric) {
  // Read breakdown
  int32_t indepReads = 0;     // Reads without correspoding writes
  int32_t depShortReads = 0;  // Reads connected to one write
  int32_t depLongReads = 0;   // Reads with multiple corresponding writes
  analyzeDependTypes(vTraceData, mReadCentric, true, &indepReads,
                     &depShortReads, &depLongReads);
  std::cout << "[Read BD]\tIndependent\tDep_Short\tDep_Long" << std::endl;
  std::cout << indepReads << "\t" << depShortReads << "\t" << depLongReads
            << std::endl;

  // Write breakdown
  int32_t indepWrites = 0;     // Writes without correspoding writes
  int32_t depShortWrites = 0;  // Writes connected to one write
  int32_t depLongWrites = 0;   // Writes with multiple corresponding writes
  analyzeDependTypes(vTraceData, mWriteCentric, false, &indepWrites,
                     &depShortWrites, &depLongWrites);
  std::cout << "[Write BD]\tIndependent\tDep_Short\tDep_Long" << std::endl;
  std::cout << indepWrites << "\t" << depShortWrites << "\t" << depLongWrites
            << std::endl;

  analyzeHotWrite(vTraceData, mWriteCentric);
}