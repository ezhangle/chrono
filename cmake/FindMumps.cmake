
##########################
UNSET(MUMPS_FOUND)
IF (NOT MUMPS_ROOT)
	SET(MUMPS_ROOT "C:/Program Files/Mumps" CACHE PATH "Mumps root directory" FORCE)
ENDIF()
SET(MUMPS_USE_MPI FALSE CACHE BOOL "Select if MPI is needed" FORCE)
SET(MUMPS_USE_DEFAULT_ORDERINGS TRUE CACHE BOOL "Select if default ordering library should be used" FORCE)

UNSET(MUMPS_ONLY_LIBRARY_COMMON CACHE)
UNSET(MUMPS_ONLY_LIBRARY_ARITHMETIC_SPECIFIC CACHE)
UNSET(MUMPS_ONLY_INCLUDE_DIRS CACHE)
UNSET(MUMPS_ONLY_LIBRARIES CACHE)
UNSET(MUMPS_MPI_INCLUDE_DIRS CACHE)
UNSET(MUMPS_MPI_LIBRARIES CACHE)
UNSET(MUMPS_ORDERINGS_INCLUDE_DIRS CACHE)
UNSET(MUMPS_ORDERINGS_LIBRARIES CACHE)
UNSET(MUMPS_INCLUDE_DIRS CACHE)
UNSET(MUMPS_LIBRARIES CACHE)
UNSET(MUMPS_SHARED_LIBRARIES CACHE)

################# Mumps library itself #####################
UNSET(MUMPS_ONLY_FOUND)
find_library(MUMPS_ONLY_LIBRARY_COMMON
			 "libmumps_common"
			 PATHS ${MUMPS_ROOT}
			 PATH_SUFFIXES "lib" "lib64" "libraries"
         )
		 
find_library(MUMPS_ONLY_LIBRARY_ARITHMETIC_SPECIFIC
			 "lib${MUMPS_ARITH_PREFIX}mumps"
			 PATHS ${MUMPS_ROOT}
			 PATH_SUFFIXES "lib" "lib64" "libraries"
         )
         
find_file (MUMPS_SHARED_LIBRARIES
          "lib${MUMPS_ARITH_PREFIX}mumps${CMAKE_SHARED_LIBRARY_SUFFIX}"
          PATHS ${MUMPS_ROOT}
	      PATH_SUFFIXES "bin"
         )

find_path(MUMPS_ONLY_INCLUDE_DIRS
          NAMES "${MUMPS_ARITH_PREFIX}mumps_c.h"
          PATHS ${MUMPS_ROOT}
          PATH_SUFFIXES "include" "inc"
         )
		 
IF (MUMPS_ONLY_LIBRARY_ARITHMETIC_SPECIFIC AND MUMPS_ONLY_INCLUDE_DIRS)
	SET(MUMPS_ONLY_LIBRARIES ${MUMPS_ONLY_LIBRARY_ARITHMETIC_SPECIFIC})
	IF ((MUMPS_IS_SHARED_LIB AND MUMPS_SHARED_LIBRARIES) OR MUMPS_ONLY_LIBRARY_COMMON)
		SET(MUMPS_ONLY_FOUND TRUE)
		IF (MUMPS_ONLY_LIBRARY_COMMON)
			SET(MUMPS_ONLY_LIBRARIES ${MUMPS_ONLY_LIBRARY_COMMON} ${MUMPS_ONLY_LIBRARY_ARITHMETIC_SPECIFIC})
		ENDIF()
	ENDIF()
endif()

################# MPI library #####################
UNSET(MUMPS_MPI_FOUND)
IF (MUMPS_USE_MPI)
	MESSAGE("MPI search is WIP.")
	find_path(MUMPS_MPI_INCLUDE_DIRS
				mpi.h
	)
	
	find_library(MUMPS_MPI_LIBRARIES
				libmpi
	)
	SET(MUMPS_MPI_FOUND TRUE)
ELSE (MUMPS_USE_MPI)

	# TODO: have they to be included?
	find_path(MUMPS_MPI_INCLUDE_DIRS
				mpi.h
				PATHS ${MUMPS_ROOT}
				PATH_SUFFIXES "include" "libseq" "libseqmpi" "libseq/include" "libseqmpi/include"
				NO_DEFAULT_PATH
			    NO_SYSTEM_ENVIRONMENT_PATH
	)
	
	find_library(MUMPS_MPI_LIBRARIES
				libmpiseq
				PATHS ${MUMPS_ROOT}
				PATH_SUFFIXES "libseq" "libseqmpi" "lib" "lib64"
				NO_DEFAULT_PATH
			    NO_SYSTEM_ENVIRONMENT_PATH
	)
	
ENDIF (MUMPS_USE_MPI)

IF (MUMPS_MPI_INCLUDE_DIRS AND (MUMPS_IS_SHARED_LIB OR MUMPS_MPI_LIBRARIES))
	SET(MUMPS_MPI_FOUND TRUE)
endif()


################# Ordering library #####################
UNSET(MUMPS_ORDERINGS_FOUND)
IF (MUMPS_USE_DEFAULT_ORDERINGS)
	# TODO: have they to be included?
	# find_path(MUMPS_ORDERINGS_INCLUDE_DIRS
				# protos.h
				# PATHS ${MUMPS_ROOT}
				# PATH_SUFFIXES "PORD/include" "PORD" "include"
	# )
	
	find_library(MUMPS_ORDERINGS_LIBRARIES
				libpord
				PATHS ${MUMPS_ROOT}
				PATH_SUFFIXES "PORD/lib" "PORD" "PORD/libraries" "lib" "lib64"
	)
	
ELSE (MUMPS_USE_DEFAULT_ORDERINGS)
	MESSAGE("Non-default orderings is not implemented yet.")
	# SET(MUMPS_ORDERINGS_LIBRARIES "" CACHE PATH "Path to ordering library" FORCE)
ENDIF (MUMPS_USE_DEFAULT_ORDERINGS)

IF (MUMPS_ORDERINGS_LIBRARIES)
	SET(MUMPS_ORDERINGS_FOUND TRUE)
ENDIF()


################# Clean up #####################
MARK_AS_ADVANCED(FORCE MUMPS_ONLY_LIBRARY_COMMON)
MARK_AS_ADVANCED(FORCE MUMPS_ONLY_LIBRARY_ARITHMETIC_SPECIFIC)
MARK_AS_ADVANCED(FORCE MUMPS_ONLY_INCLUDE_DIRS)
MARK_AS_ADVANCED(FORCE MUMPS_ONLY_LIBRARIES)
MARK_AS_ADVANCED(FORCE MUMPS_MPI_INCLUDE_DIRS)
MARK_AS_ADVANCED(FORCE MUMPS_MPI_LIBRARIES)
MARK_AS_ADVANCED(FORCE MUMPS_ORDERINGS_INCLUDE_DIRS)
MARK_AS_ADVANCED(FORCE MUMPS_ORDERINGS_LIBRARIES)
MARK_AS_ADVANCED(FORCE MUMPS_INCLUDE_DIRS)
MARK_AS_ADVANCED(FORCE MUMPS_LIBRARIES)
MARK_AS_ADVANCED(FORCE MUMPS_SHARED_LIBRARIES)

################# Exported variables #####################		
IF (MUMPS_ONLY_FOUND)
	SET(MUMPS_INCLUDE_DIRS ${MUMPS_ONLY_INCLUDE_DIRS} ${MUMPS_MPI_INCLUDE_DIRS} ${MUMPS_ORDERINGS_INCLUDE_DIRS})
    list(REMOVE_DUPLICATES MUMPS_INCLUDE_DIRS)
	IF (MUMPS_IS_SHARED_LIB AND MUMPS_SHARED_LIBRARIES)
		SET(MUMPS_LIBRARIES ${MUMPS_ONLY_LIBRARIES})
		SET(MUMPS_FOUND TRUE)
		MESSAGE(STATUS "Mumps found as shared library.")
	ELSEIF (MUMPS_MPI_FOUND AND MUMPS_ORDERINGS_FOUND)
		SET(MUMPS_LIBRARIES ${MUMPS_ONLY_LIBRARIES} ${MUMPS_MPI_LIBRARIES} ${MUMPS_ORDERINGS_LIBRARIES})
		SET(MUMPS_FOUND TRUE)
		MESSAGE(STATUS "Mumps found as static library.")
	ELSE ()
		MESSAGE(STATUS "Mumps found, but other required libraries are missing.")
		SET(MUMPS_FOUND "MUMPS-NOTFOUND")
	ENDIF()
ELSE()
	MESSAGE(STATUS "Mumps not found.")
    SET(MUMPS_FOUND "MUMPS-NOTFOUND")
ENDIF()

if(MUMPS_FOUND)
	MARK_AS_ADVANCED(FORCE MUMPS_ROOT)
	MARK_AS_ADVANCED(FORCE MUMPS_USE_MPI)
	MARK_AS_ADVANCED(FORCE MUMPS_USE_DEFAULT_ORDERINGS)
endif()