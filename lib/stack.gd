#############################################################################
##
#W  stack.gd                 GAP Library                    Markus Pfeiffer
##
##
#Y  (C) 2013 School of Computer Science, University of St Andrews, Scotland
##
##
##  Stack implementation


## Stack constructor
DeclareGlobalFunction("Stack");

## Operations for stacks

## Stacks implemented as Lists
DeclareOperation( "Push", [ IsList, IsObject ] );
DeclareOperation( "Pop", [ IsList ] );
DeclareOperation( "Peek", [ IsList ] );
