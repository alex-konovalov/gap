#############################################################################
##
#W  stack.gi                 GAP Library                    Markus Pfeiffer
##
##
#Y  (C) 2013 School of Computer Science, University of St Andrews, Scotland
##
##
##  Stack implementation

InstallGlobalFunction("NewStack",
        function()
    return [];
end);

InstallMethod( Push
        , "generic method for stacks"
        , true
        , [ IsList, IsObject ]
        , 0
        , function( s, d )
    Add(s, d);
end);

InstallMethod( Pop
        , "generic method for stacks"
        , true
        , [ IsList ]
        , 0
        , function( s )
    return Remove( s );
end);

InstallMethod( Peek
        , "generic method for stacks"
        , true
        , [ IsList ]
        , 0
        , function( s )
    return s[Length(s)];
end);


