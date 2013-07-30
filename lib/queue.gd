#############################################################################
##
#W  queue.gd                 GAP Library                    Markus Pfeiffer
##
##
#Y  (C) 2013 School of Computer Science, University of St Andrews, Scotland
##
##
##  Queue implementation


DeclareGlobalFunction("Queue");


## Queue implementation as List
DeclareOperation( "Enqueue", [ IsList, IsObject ] );
DeclareOperation( "Dequeue", [ IsList ] );


