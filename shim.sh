#!/bin/bash
gcc -E ${MPI_INCLUDE_PATH}/mpi.h | 	# Preprocess 		\
tr -d "\n" | 		# remove all carriage returns		\
tr -s " " | 		# remove all excess whitespace		\
tr ";" "\n" | 		# replace ';' with CR (each statement 	\
			#   now takes one line)			\
grep " MPI_"  | 	# Find all the lines that have MPI_	\
grep -v "^#" | 		# ...that aren't comments...		\
grep -v "^typedef" | 	# ...or typedefs...			\
grep -v "^enum" | 	# ...or enums...			\
grep -v "^struct" | 	# ...or structs...			\
grep -v " OMPI" | 	# ...or OMPI calls...			\
grep -v " PMPI_" | 	# ...or PMPI calls...			\
grep "(" |  		# and are function calls.		\
			# There's one case of "type * var", 	\
			# convert to "type *var".		\
sed 's/\* /*/' | 	#					\
			# Handle this annoying problem:  [][3]	\
sed 's/ \([\(A-Za-z_\)]*\)\[\]\[3\]/ **\1/g' | 	#		\
			# convert "type var[]" to "type *var"	\
sed 's/ \([\(A-Za-z_\)]*\)\[\]/ *\1/g' |	#		\
python ./shim.py
patch < _PATCH_knockouts.shim_selection.h

