#############################################################################
##
#W  queue.gi                 GAP Library                    Markus Pfeiffer
##
##
#Y  (C) 2013 School of Computer Science, University of St Andrews, Scotland
##
##
##  Queue implementation
##
##  For starters a very simple queue implementation based on lists. A queue q
##  of capacity cap is a list with 4 entries:
##  q[1] is cap,
##  q[2] head
##  q[3] tail
##  q[4] is the queue itself
##
##  TODO: Wrap this into an object?

_Q_CAP := 1;
_Q_HEAD := 2;
_Q_TAIL := 3;
_Q_QUEUE := 4;

InstallGlobalFunction( Queue
        , function( cap )
    return([cap,0,0,[]]);
end);

InstallMethod( Enqueue
        , "generic method for queues implemented as lists"
        , true
        , [ IsList, IsObject ]
        , 0
        , function( q, d )
    if ((q[_Q_HEAD] - q[_Q_TAIL] + 1) mod q[_Q_CAP]) <> 0 then
        q[_Q_QUEUE][q[_Q_HEAD] + 1] := d;              # GAP lists start with index 1
        q[_Q_HEAD] := (q[_Q_HEAD] + 1) mod q[_Q_CAP];
    else
        Error("Queue is full");
    fi;
end);

InstallMethod( Dequeue
        , "generic method for queues implemented as lists"
        , true
        , [ IsList ]
        , 0
        , function( q )
    local ret;

    if (q[_Q_HEAD] <> q[_Q_TAIL]) then
        ret := q[_Q_QUEUE][q[_Q_TAIL] + 1];
        q[_Q_QUEUE][q[_Q_TAIL] + 1] := -1;
        q[_Q_TAIL] := (q[_Q_TAIL] + 1) mod q[_Q_CAP];
        return ret;
    else
        Error("Queue is empty");
    fi;
end);



