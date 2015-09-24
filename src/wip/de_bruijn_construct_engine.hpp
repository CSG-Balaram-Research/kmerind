/*
 * de_bruijn_construct_engine.hpp
 *
 *  Created on: Aug 17, 2015
 *      Author: yongchao
 */

#ifndef DE_BRUIJN_CONSTRUCT_ENGINE_HPP_
#define DE_BRUIJN_CONSTRUCT_ENGINE_HPP_


#if defined(USE_MPI)
#include "mpi.h"
#endif

//#if defined(USE_OPENMP)
//#include "omp.h"
//#endif

#include <unistd.h>     // sysconf
#include <sys/stat.h>   // block size.
#include <tuple>        // tuple and utility functions
#include <utility>      // pair and utility functions.
#include <type_traits>
#include <cctype>       // tolower.

#include "io/fastq_loader.hpp"
#include "io/fasta_loader.hpp"
//#include "io/fasta_iterator.hpp"

#include "utils/logging.h"
#include "common/alphabets.hpp"
#include "common/kmer.hpp"
#include "common/base_types.hpp"
#include "common/sequence.hpp"
#include "utils/kmer_utils.hpp"

#include "io/mxx_support.hpp"
#include "containers/distributed_unordered_map.hpp"
#include "containers/distributed_sorted_map.hpp"
// way too slow.  also not updated. #include <containers/distributed_map.hpp>

#include "io/sequence_iterator.hpp"
#include "io/sequence_id_iterator.hpp"
#include "iterators/transform_iterator.hpp"
#include "common/kmer_iterators.hpp"
#include "iterators/zip_iterator.hpp"
#include "iterators/constant_iterator.hpp"
#include "index/quality_score_iterator.hpp"
#include "wip/kmer_hash.hpp"
#include "wip/de_bruijn_node_trait.hpp"
#include "iterators/edge_iterator.hpp"
#include "wip/kmer_index.hpp"

#include "utils/timer.hpp"

namespace bliss
{
  namespace de_bruijn
  {
//    	// generic class to create a directed de Bruijn graph
      // reuses the kmer index class
      // only need to supply correct de bruijn parser.


  /*generate de Bruijn graph nodes and edges from the reads*/
    /**
     * @tparam TupleType  value type of outputIt, or the generated type.  supplied so that we can use template template param with Index.
     */
   template <typename KmerType, typename EdgeEncoder = bliss::common::DNA16>
	 struct de_bruijn_parser {

      /// type of element generated by this parser.  since kmer itself is parameterized, this is not hard coded.  NOTE THAT THIS IS TYPE FOR THE OUTPUTIT.
      using edge_type = typename std::conditional<std::is_same<EdgeEncoder, bliss::common::ASCII>::value,
                                                  uint16_t, uint8_t>::type;
      using value_type = std::pair<KmerType, edge_type>;

      using Alphabet = typename KmerType::KmerAlphabet;

		 template <typename SeqType, typename OutputIt>
		 OutputIt operator()(SeqType & read, OutputIt output_iter) {
		   static_assert(std::is_same<value_type, typename ::std::iterator_traits<OutputIt>::value_type>::value,
		                  "output type and output container value type are not the same");

	      // filter out EOL characters
	       using CharIter = bliss::index::kmer::NonEOLIter<typename SeqType::IteratorType>;
	       // converter from ascii to alphabet values
	       using BaseCharIterator = bliss::iterator::transform_iterator<CharIter, bliss::common::ASCII2<Alphabet> >;
	       // kmer generation iterator
	       using KmerIter = bliss::common::KmerGenerationIterator<BaseCharIterator, KmerType>;


		    // edge iterator type
//		   using EdgeType = typename ::std::tuple_element<1, TupleType>::type;
//		   static_assert((std::is_same<EdgeEncoder, bliss::common::ASCII>::value && std::is_same<EdgeType, uint16_t>::value) ||
//		                 std::is_same<EdgeType, uint8_t>::value, "Edge Type is not compatible with EdgeEncoder type");

       using EdgeIterType = bliss::iterator::edge_iterator<CharIter, EdgeEncoder>;


		   // combine kmer iterator and position iterator to create an index iterator type.
		   using KmerIndexIterType = bliss::iterator::ZipIterator<KmerIter, EdgeIterType>;

		    static_assert(std::is_same<typename std::iterator_traits<KmerIndexIterType>::value_type,
		                  value_type>::value,
		        "Generating iterator and output iterator's value types differ");

		    // then compute and store into index (this will generate kmers and insert into index)
		     if (read.seqBegin == read.seqEnd) return output_iter;

		     //== set up the kmer generating iterators.
		     bliss::index::kmer::NotEOL neol;
		     KmerIter start(BaseCharIterator(CharIter(neol, read.seqBegin, read.seqEnd), bliss::common::ASCII2<Alphabet>()), true);
		     KmerIter end(BaseCharIterator(CharIter(neol, read.seqEnd), bliss::common::ASCII2<Alphabet>()), false);


		     // set up edge iterator.  returns the left and right chars around the kmer IN THE READ.
	       EdgeIterType edge_start(CharIter(neol, read.seqBegin, read.seqEnd), CharIter(neol, read.seqEnd), KmerType::size);
	       EdgeIterType edge_end (CharIter(neol, read.seqEnd));


	       // ==== set up the zip iterators
	       KmerIndexIterType node_start (start, edge_start);
	       KmerIndexIterType node_end(end, edge_end);

	       return ::std::copy(node_start, node_end, output_iter);

		 }
	 };

	 /*generate de Brujin graph nodes and edges, which each node associated with base quality scores*/
    template <typename KmerType, typename QualType=double, typename EdgeEncoder = bliss::common::DNA16, template<typename> class QualityEncoder = bliss::index::Illumina18QualityScoreCodec>
	 struct de_bruijn_quality_parser {

        /// type of element generated by this parser.  since kmer itself is parameterized, this is not hard coded.  NOTE THAT THIS IS TYPE FOR THE OUTPUTIT.
        using edge_type = typename std::conditional<std::is_same<EdgeEncoder, bliss::common::ASCII>::value,
                                                    uint16_t, uint8_t>::type;
        using value_type = std::pair<KmerType, std::pair<edge_type, QualType> >;

        using Alphabet = typename KmerType::KmerAlphabet;

       template <typename SeqType, typename OutputIt>
       OutputIt operator()(SeqType & read, OutputIt output_iter) {

         static_assert(std::is_same<value_type, typename ::std::iterator_traits<OutputIt>::value_type>::value,
                        "output type and output container value type are not the same");


         // filter out EOL characters
          using CharIter = bliss::index::kmer::NonEOLIter<typename SeqType::IteratorType>;
          // converter from ascii to alphabet values
          using BaseCharIterator = bliss::iterator::transform_iterator<CharIter, bliss::common::ASCII2<Alphabet> >;
          // kmer generation iterator
          using KmerIter = bliss::common::KmerGenerationIterator<BaseCharIterator, KmerType>;

          // edge iterator type
//          using EdgeType = typename std::tuple_element<0, typename std::tuple_element<1, TupleType>::type >::type;
//          static_assert((std::is_same<EdgeEncoder, bliss::common::ASCII>::value && std::is_same<EdgeType, uint16_t>::value) ||
//                          std::is_same<EdgeType, uint8_t>::value, "Edge Type is not compatible with EdgeEncoder type");

          using EdgeIterType = bliss::iterator::edge_iterator<CharIter, EdgeEncoder>;

          // also remove eol from quality score
          using QualIterType =
              bliss::index::QualityScoreGenerationIterator<bliss::index::kmer::NonEOLIter<typename SeqType::IteratorType>, KmerType::size, QualityEncoder<QualType> >;

          // combine kmer iterator and position iterator to create an index iterator type.
          using KmerInfoIterType = bliss::iterator::ZipIterator<EdgeIterType, QualIterType>;

          using KmerIndexIterType = bliss::iterator::ZipIterator<KmerIter, KmerInfoIterType>;

          static_assert(std::is_same<typename std::iterator_traits<KmerIndexIterType>::value_type,
                                     value_type>::value,
              "generating iterator and output iterator's value types differ");


           // then compute and store into index (this will generate kmers and insert into index)
           if (read.seqBegin == read.seqEnd || read.qualBegin == read.qualEnd) return output_iter;

           //== set up the kmer generating iterators.
           bliss::index::kmer::NotEOL neol;
           KmerIter start(BaseCharIterator(CharIter(neol, read.seqBegin, read.seqEnd), bliss::common::ASCII2<Alphabet>()), true);
           KmerIter end(BaseCharIterator(CharIter(neol, read.seqEnd), bliss::common::ASCII2<Alphabet>()), false);


           // set up edge iterator.  returns the left and right chars around the kmer IN THE READ.
           EdgeIterType edge_start(CharIter(neol, read.seqBegin, read.seqEnd), CharIter(neol, read.seqEnd), KmerType::size);
           EdgeIterType edge_end (CharIter(neol, read.seqEnd));


           QualIterType qual_start(CharIter(neol, read.qualBegin, read.qualEnd));
            QualIterType qual_end(CharIter(neol, read.qualEnd));

            KmerInfoIterType info_start(edge_start, qual_start);
            KmerInfoIterType info_end(edge_end, qual_end);



           // ==== set up the zip iterators
            KmerIndexIterType node_start(start, info_start);
            KmerIndexIterType node_end(end, info_end);

           return ::std::copy(node_start, node_end, output_iter);

		 }
	 };

	template <typename MapType, typename EdgeEncoder = bliss::common::DNA16>
	using de_bruijn_engine = ::bliss::index::kmer::Index<MapType, de_bruijn_parser<typename MapType::key_type, EdgeEncoder > >;

	template <typename MapType, typename EdgeEncoder = bliss::common::DNA16, template<typename> class QualityEncoder = bliss::index::Illumina18QualityScoreCodec >
  using de_bruijn_quality_engine = ::bliss::index::kmer::Index<MapType, de_bruijn_quality_parser<typename MapType::key_type, typename std::tuple_element<1, typename MapType::mapped_type>::type, EdgeEncoder, QualityEncoder > >;

  } /* namespace de_bruijn */
} /* namespace bliss */

#endif /* DE_BRUIJN_CONSTRUCT_ENGINE_HPP_ */
