//Enable different testing mechanisms including coverage, thread
// and address sanitizers, relacy, and google test
 
-DENABLE_TESTING:BOOL=ON

//Inform whether sample applications should be built
-DBUILD_EXAMPLE_APPLICATIONS:BOOL=ON



//choose a logging engine.  options are NO_LOG PRINTF.
-DLOG_ENGINE:STRING="BOOST_TRIVIAL"
 "NO_LOG" OR "CERR" OR "PRINTF" OR "BOOST_TRIVIAL"  .* OR "BOOST_CUSTOM"

 //loger verbosit
-DLOGGER_VERBOSITY:STRING="TRACE"
 define BLISS_LOGGER_VERBOSITY_FATAL   0
#define BLISS_LOGGER_VERBOSITY_ERROR   1
#define BLISS_LOGGER_VERBOSITY_WARNING 2
#define BLISS_LOGGER_VERBOSITY_INFO    3
#define BLISS_LOGGER_VERBOSITY_DEBUG   4
#define BLISS_LOGGER_VERBOSITY_TRACE   5

cmake -DENABLE_TESTING:BOOL=ON -DBUILD_EXAMPLE_APPLICATIONS:BOOL=ON -DLOG_ENGINE:STRING="BOOST_TRIVIAL" -DLOGGER_VERBOSITY:STRING="TRACE" ..
