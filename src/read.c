/****************************************************************************
**
*W  read.c                      GAP source                   Martin Schönert
**
**
*Y  Copyright (C)  1996,  Lehrstuhl D für Mathematik,  RWTH Aachen,  Germany
*Y  (C) 1998 School Math and Comp. Sci., University of St Andrews, Scotland
*Y  Copyright (C) 2002 The GAP Group
**
**  This module contains the functions to read expressions and statements.
*/
#include        <string.h>              /* memcpy */
#include        "system.h"              /* system dependent part           */



#include        "gasman.h"              /* garbage collector               */
#include        "objects.h"             /* objects                         */
#include        "scanner.h"             /* scanner                         */

#include        "gap.h"                 /* error handling, initialisation  */

#include        "gvars.h"               /* global variables                */
#include        "string.h"              /* strings                         */
#include        "calls.h"               /* generic call mechanism          */
#include        "code.h"                /* coder                           */

#include        "records.h"             /* generic records                 */
#include        "precord.h"             /* plain records                   */

#include        "lists.h"               /* generic lists                   */
#include        "plist.h"               /* plain lists                     */


#include        "intrprtr.h"            /* interpreter                     */

#include        "read.h"                /* reader                          */

#include	"tls.h"
#include	"thread.h"

#include        "vars.h"                /* variables                       */

#include        "bool.h"

#include         <assert.h>

/****************************************************************************
**

*F  READ_ERROR()  . . . . . . . . . . . . . . . . . . . reader found an error
**
**  'READ_ERROR' returns a non-zero value if the reader found an error, or if
**  the interpretation of  an expression  or  statement lead to an  error (in
**  which case 'ReadEvalError' jumps back to 'READ_ERROR' via 'longjmp').
**
#define READ_ERROR()    (TLS->nrError || (TLS->nrError+=sySetjmp(ReadJmpError)))
*/
/* TL: syJmp_buf         ReadJmpError; */


/****************************************************************************
**
*V  StackNams . . . . . . . . . . . . .  stack of local variables names lists
*V  CountNams . . . . . . . . . number of local variables names list on stack
**
**  'StackNams' is a stack of local variables  names lists.  A new names list
**  is pushed onto this stack when the  reader begins to  read a new function
**  expression  (after  reading the argument   list  and the local  variables
**  list), and popped again when the reader has finished reading the function
**  expression (after reading the 'end').
**
**  'CountNams' is the number of local variables names lists currently on the
**  stack.
*/
/* TL: Obj             StackNams; */

/* TL: UInt            CountNams; */


/****************************************************************************
**
*V  ReadTop . . . . . . . . . . . . . . . . . . . . . .  top level expression
*V  ReadTilde . . . . . . . . . . . . . . . . . . . . . . . . . .  tilde read
**
**  'ReadTop' is 0  if the reader is   currently not reading  a  list or record
**  expression.  'ReadTop'  is 1 if the  reader is currently reading an outmost
**  list or record expression.   'ReadTop' is larger than   1 if the  reader is
**  currently reading a nested list or record expression.
**
**  'ReadTilde' is 1 if the reader has read a  reference to the global variable
**  '~' within the current outmost list or record expression.
*/
/* TL: UInt            ReadTop; */

/* TL: UInt            ReadTilde; */


/****************************************************************************
**
*V  CurrLHSGVar . . . . . . . . . . . .  current left hand side of assignment
**
**  'CurrLHSGVar' is the current left hand side of an assignment.  It is used
**  to prevent undefined global variable  warnings, when reading a  recursive
**  function.
*/
/* TL: UInt            CurrLHSGVar; */


/****************************************************************************
**
**  The constructs <Expr> and <Statments> may have themself as subpart, e.g.,
**  '<Var>( <Expr> )'  is  <Expr> and 'if   <Expr> then <Statments> fi;'   is
**  <Statments>.  The  functions 'ReadExpr' and  'ReadStats' must therefor be
**  declared forward.
*/
void            ReadExpr (
    TypSymbolSet        follow,
    Char                mode );

UInt            ReadStats (
    TypSymbolSet        follow );

void            ReadFuncExpr1 (
    TypSymbolSet        follow );

void            ReadFuncExpr0 (
    TypSymbolSet        follow );

void ReadAtom (
    TypSymbolSet        follow,
    Char                mode );

/* TL: static UInt CurrentGlobalForLoopVariables[100]; */
/* TL: static UInt CurrentGlobalForLoopDepth; */

void PushGlobalForLoopVariable( UInt var)
{
  if (TLS->currentGlobalForLoopDepth < 100)
    TLS->currentGlobalForLoopVariables[TLS->currentGlobalForLoopDepth] = var;
  TLS->currentGlobalForLoopDepth++;
}

void PopGlobalForLoopVariable( void )
{
  assert(TLS->currentGlobalForLoopDepth);
  TLS->currentGlobalForLoopDepth--;
}

UInt GlobalComesFromEnclosingForLoop (UInt var)
{
  UInt i;
  for (i = 0; i < TLS->currentGlobalForLoopDepth; i++)
    {
      if (i==100)
	return 0;
      if (TLS->currentGlobalForLoopVariables[i] == var)
	return 1;
    }
  return 0;
}

/****************************************************************************
**
*F * * * * * * * * * * read symbols and call interpreter  * * * * * * * * * *
*/


/****************************************************************************
**

*F  ReadCallVarAss( <follow>, <mode> )  . . . . . . . . . . . read a variable
**
**  'ReadCallVarAss' reads  a variable.  In  case  of an  error it skips  all
**  symbols up to one contained in  <follow>.  The <mode>  must be one of the
**  following:
**
**  'i':        check if variable, record component, list entry is bound
**  'r':        reference to a variable
**  's':        assignment via ':='
**  'u':        unbind a variable
**  'x':        either 'r' or 's' depending on <Symbol>
**
**  <Ident> :=  a|b|..|z|A|B|..|Z { a|b|..|z|A|B|..|Z|0|..|9|_ }
**
**  <Var> := <Ident>
**        |  <Var> '[' <Expr> ']'
**        |  <Var> '{' <Expr> '}'
**        |  <Var> '.' <Ident>
**        |  <Var> '(' [ <Expr> { ',' <Expr> } ] [':' [ <options> ]] ')'
*/
extern Obj ExprGVars[GVAR_BUCKETS];
/* TL: extern Obj ErrorLVars; */
/* TL: extern Obj BottomLVars; */

/* This function reads the options part at the end of a function call
   The syntax is

   <options> := <option> [, <options> ]
   <option>  := <Ident> | '(' <Expr> ')' [ ':=' <Expr> ]

   empty options lists are handled further up
*/
void ReadFuncCallOption( TypSymbolSet follow )
{
  volatile UInt       rnam;           /* record component name           */
  if ( TLS->symbol == S_IDENT ) {
    rnam = RNamName( TLS->value );
    Match( S_IDENT, "identifier", S_COMMA | follow );
    if ( ! READ_ERROR() ) { IntrFuncCallOptionsBeginElmName( rnam ); }
  }
  else if ( TLS->symbol == S_LPAREN ) {
    Match( S_LPAREN, "(", S_COMMA | follow );
    ReadExpr( follow, 'r' );
    Match( S_RPAREN, ")", S_COMMA | follow );
    if ( ! READ_ERROR() ) { IntrFuncCallOptionsBeginElmExpr(); }
  }
  else {
    SyntaxError("identifier expected");
  }
  if ( TLS->symbol == S_ASSIGN )
    {
      Match( S_ASSIGN, ":=", S_COMMA | follow );
      ReadExpr( S_COMMA | S_RPAREN|follow, 'r' );
      if ( ! READ_ERROR() ) { IntrFuncCallOptionsEndElm(); }
    }
  else
    {
      if ( ! READ_ERROR() ) { IntrFuncCallOptionsEndElmEmpty(); }
    }
  return;
}

void ReadFuncCallOptions( TypSymbolSet follow )
{
  volatile UInt nr;
  if ( ! READ_ERROR() ) { IntrFuncCallOptionsBegin( ); }
  ReadFuncCallOption( follow);
  nr = 1;
  while ( TLS->symbol == S_COMMA )
    {
      Match(S_COMMA, ",", follow );
      ReadFuncCallOption( follow );
      nr++;
    }
  if (!READ_ERROR()) {
    IntrFuncCallOptionsEnd( nr );
  }

  return;
}

static Obj GAPInfo;

static UInt WarnOnUnboundGlobalsRNam;

void ReadCallVarAss (
    TypSymbolSet        follow,
    Char                mode )
{
    volatile Char       type = ' ';     /* type of variable                */
    volatile Obj        nams;           /* list of names of local vars.    */
    volatile Obj        lvars;          /* environment                     */
    volatile UInt       nest  = 0;      /* nesting level of a higher var.  */
    volatile Obj        lvars0;          /* environment                     */
    volatile UInt       nest0  = 0;      /* nesting level of a higher var.  */
    volatile UInt       indx  = 0;      /* index of a local variable       */
    volatile UInt       var   = 0;      /* variable                        */
    volatile UInt       level = 0;      /* number of '{}' selectors        */
    volatile UInt       rnam  = 0;      /* record component name           */
    volatile UInt       narg  = 0;      /* number of arguments             */


    /* all variables must begin with an identifier                         */
    if ( TLS->symbol != S_IDENT ) {
        SyntaxError( "identifier expected" );
        return;
    }

    /* try to look up the variable on the stack of local variables         */
    nest = 0;
    while ( type == ' ' && nest < TLS->countNams ) {
        nams = ELM_LIST( TLS->stackNams, TLS->countNams-nest );
        for ( indx = LEN_LIST( nams ); 1 <= indx; indx-- ) {
            if ( strcmp( TLS->value, CSTR_STRING(ELM_LIST(nams,indx)) ) == 0 ) {
                if ( nest == 0 ) {
                    type = 'l';
                    var = indx;
                }
                else {
                    type = 'h';

                    /* Ultrix 4.2 cc get's confused if the UInt is missing */
                    var = ((UInt)nest << 16) + indx;
                }
                break;
            }
        }
        nest++;
    }

    /* try to look up the variable on the error stack                      */
    /* the outer loop runs up the calling stack, while the inner loop runs
       up the static definition stack for each call function */
    lvars0 = TLS->errorLVars;
    nest0 = 0;
    while ( type == ' ' && lvars0 != 0 && lvars0 != TLS->bottomLVars) {
      lvars = lvars0;
      nest = 0;
      while ( type == ' ' && lvars != 0 && lvars != TLS->bottomLVars ) {
	nams = NAMS_FUNC(PTR_BAG(lvars)[0]);
	if (nams != (Obj) 0)
	  {
	    indx = LEN_LIST( nams );
	    if (indx >= 1024)
	      {
		Pr("Warning; Ignoring local names after 1024th in search for %s\n",
		   (Int) TLS->value,
		   0L);
		indx = 1023;
	      }
	    for ( ; 1 <= indx; indx-- ) {
	      if ( strcmp( TLS->value, CSTR_STRING(ELM_LIST(nams,indx)) ) == 0 ) {
		type = 'd';

		/* Ultrix 4.2 cc get's confused if the UInt is missing     */
		var = ((UInt)nest << 16) + indx;
		break;
	      }
	    }
        }
        lvars = ENVI_FUNC( PTR_BAG( lvars )[0] );
        nest++;
	if (nest >= 65536)
	  {
	    Pr("Warning: abandoning search for %s at 1024th higher frame\n",
	       (Int)TLS->value,0L);
	    break;
	  }
      }
      lvars0 = PTR_BAG( lvars0 )[2];
      nest0++;
	if (nest0 >= 65536)
	  {
	    Pr("Warning: abandoning search for %s 4096 frames up stack\n",
	       (Int)TLS->value,0L);
	    break;
	  }
    }

    /* get the variable as a global variable                               */
    if ( type == ' ' ) {
        type = 'g';
        var = GVarName( TLS->value );
    }

    /* match away the identifier, now that we know the variable            */
    Match( S_IDENT, "identifier", follow );

    /* if this was actually the beginning of a function literal            */
    /* then we are in the wrong function                                   */
    if ( TLS->symbol == S_MAPTO ) {
      if (mode == 'r' || mode == 'x')
	{
	  ReadFuncExpr1( follow );
	  return;
	}
      else
	SyntaxError("function literal in impossible context");
    }

    /* check whether this is an unbound global variable                    */

    if (WarnOnUnboundGlobalsRNam == 0)
      WarnOnUnboundGlobalsRNam = RNamName("WarnOnUnboundGlobals");

    if ( type == 'g'
      && TLS->countNams != 0
      && var != TLS->currLHSGVar
      && var != Tilde
      && VAL_GVAR(var) == 0
      && ELM_PLIST(ExprGVars[GVAR_BUCKET(var)], GVAR_INDEX(var)) == 0
      && ! TLS->intrIgnoring
      && ! GlobalComesFromEnclosingForLoop(var)
      && (GAPInfo == 0 || !IS_REC(GAPInfo) || !ISB_REC(GAPInfo,WarnOnUnboundGlobalsRNam) ||
             ELM_REC(GAPInfo,WarnOnUnboundGlobalsRNam) != False )
      && ! SyCompilePlease )
    {
        SyntaxError("warning: unbound global variable");
        TLS->nrError--;
        TLS->nrErrLine--;
    }

    /* check whether this is a reference to the global variable '~'        */
    if ( type == 'g' && var == Tilde ) { TLS->readTilde = 1; }

    /* followed by one or more selectors                                   */
    while ( IS_IN( TLS->symbol, S_LPAREN|S_LBRACK|S_LBRACE|S_DOT ) ) {

        /* so the prefix was a reference                                   */
      if ( READ_ERROR() ) {}
        else if ( type == 'l' ) { IntrRefLVar( var );           level=0; }
        else if ( type == 'h' ) { IntrRefHVar( var );           level=0; }
        else if ( type == 'd' ) { IntrRefDVar( var, nest0 - 1 );           level=0; }
        else if ( type == 'g' ) { IntrRefGVar( var );           level=0; }
        else if ( type == '[' ) { IntrElmList();                         }
        else if ( type == ']' ) { IntrElmListLevel( level );             }
        else if ( type == '{' ) { IntrElmsList();               level++; }
        else if ( type == '}' ) { IntrElmsListLevel( level );   level++; }
        else if ( type == '<' ) { IntrElmPosObj();                       }
        else if ( type == '>' ) { IntrElmPosObjLevel( level );           }
        else if ( type == '(' ) { IntrElmsPosObj();             level++; }
        else if ( type == ')' ) { IntrElmsPosObjLevel( level ); level++; }
        else if ( type == '.' ) { IntrElmRecName( rnam );       level=0; }
        else if ( type == ':' ) { IntrElmRecExpr();             level=0; }
        else if ( type == '!' ) { IntrElmComObjName( rnam );    level=0; }
        else if ( type == '|' ) { IntrElmComObjExpr();          level=0; }
        else if ( type == 'c' || type == 'C' )
	  { IntrFuncCallEnd( 1UL, type == 'C', narg ); level=0; }

        /* <Var> '[' <Expr> ']'  list selector                             */
        if ( TLS->symbol == S_LBRACK ) {
            Match( S_LBRACK, "[", follow );
            ReadExpr( S_RBRACK|follow, 'r' );
            Match( S_RBRACK, "]", follow );
            type = (level == 0 ? '[' : ']');
        }

        /* <Var> '{' <Expr> '}'  sublist selector                          */
        else if ( TLS->symbol == S_LBRACE ) {
            Match( S_LBRACE, "{", follow );
            ReadExpr( S_RBRACE|follow, 'r' );
            Match( S_RBRACE, "}", follow );
            type = (level == 0 ? '{' : '}');
        }

        /* <Var> '![' <Expr> ']'  list selector                            */
        else if ( TLS->symbol == S_BLBRACK ) {
            Match( S_BLBRACK, "![", follow );
            ReadExpr( S_RBRACK|follow, 'r' );
            Match( S_RBRACK, "]", follow );
            type = (level == 0 ? '<' : '>');
        }

        /* <Var> '!{' <Expr> '}'  sublist selector                         */
        else if ( TLS->symbol == S_BLBRACE ) {
            Match( S_BLBRACE, "!{", follow );
            ReadExpr( S_RBRACE|follow, 'r' );
            Match( S_RBRACE, "}", follow );
            type = (level == 0 ? '(' : ')');
        }

        /* <Var> '.' <Ident>  record selector                              */
        else if ( TLS->symbol == S_DOT ) {
            Match( S_DOT, ".", follow );
            if ( TLS->symbol == S_IDENT || TLS->symbol == S_INT ) {
                rnam = RNamName( TLS->value );
                Match( TLS->symbol, "identifier", follow );
                type = '.';
            }
            else if ( TLS->symbol == S_LPAREN ) {
                Match( S_LPAREN, "(", follow );
                ReadExpr( S_RPAREN|follow, 'r' );
                Match( S_RPAREN, ")", follow );
                type = ':';
            }
            else {
                SyntaxError("record component name expected");
            }
            level = 0;
        }

        /* <Var> '!.' <Ident>  record selector                             */
        else if ( TLS->symbol == S_BDOT ) {
            Match( S_BDOT, "!.", follow );
            if ( TLS->symbol == S_IDENT || TLS->symbol == S_INT ) {
                rnam = RNamName( TLS->value );
                Match( TLS->symbol, "identifier", follow );
                type = '!';
            }
            else if ( TLS->symbol == S_LPAREN ) {
                Match( S_LPAREN, "(", follow );
                ReadExpr( S_RPAREN|follow, 'r' );
                Match( S_RPAREN, ")", follow );
                type = '|';
            }
            else {
                SyntaxError("record component name expected");
            }
            level = 0;
        }

        /* <Var> '(' [ <Expr> { ',' <Expr> } ] ')'  function call          */
        else if ( TLS->symbol == S_LPAREN ) {
            Match( S_LPAREN, "(", follow );
            if ( ! READ_ERROR() ) { IntrFuncCallBegin(); }
            narg = 0;
            if ( TLS->symbol != S_RPAREN && TLS->symbol != S_COLON) {
                ReadExpr( S_RPAREN|follow, 'r' );
                narg++;
            }
            while ( TLS->symbol == S_COMMA ) {
                Match( S_COMMA, ",", follow );
                ReadExpr( S_RPAREN|follow, 'r' );
                narg++;
            }
	    type = 'c';
	    if (TLS->symbol == S_COLON ) {
	      Match( S_COLON, ":", follow );
	      if ( TLS->symbol != S_RPAREN ) /* save work for empty options */
		{
		  ReadFuncCallOptions(S_RPAREN | follow);
		  type = 'C';
		}
	    }
	    Match( S_RPAREN, ")", follow );
        }

    }

    /* if we need a reference                                              */
    if ( mode == 'r' || (mode == 'x' && !IS_IN(TLS->symbol, S_ASSIGN)) ) {
        if ( READ_ERROR() ) {}
        else if ( type == 'l' ) { IntrRefLVar( var );           }
        else if ( type == 'h' ) { IntrRefHVar( var );           }
        else if ( type == 'd' ) { IntrRefDVar( var, nest0 - 1 );           }
        else if ( type == 'g' ) { IntrRefGVar( var );           }
        else if ( type == '[' ) { IntrElmList();                }
        else if ( type == ']' ) { IntrElmListLevel( level );    }
        else if ( type == '{' ) { IntrElmsList();               }
        else if ( type == '}' ) { IntrElmsListLevel( level );   }
        else if ( type == '<' ) { IntrElmPosObj();                }
        else if ( type == '>' ) { IntrElmPosObjLevel( level );    }
        else if ( type == '(' ) { IntrElmsPosObj();               }
        else if ( type == ')' ) { IntrElmsPosObjLevel( level );   }
        else if ( type == '.' ) { IntrElmRecName( rnam );       }
        else if ( type == ':' ) { IntrElmRecExpr();             }
        else if ( type == '!' ) { IntrElmComObjName( rnam );      }
        else if ( type == '|' ) { IntrElmComObjExpr();            }
        else if ( type == 'c' || type == 'C') {
            if ( mode == 'x' && TLS->symbol == S_SEMICOLON ) {
                IntrFuncCallEnd( 0UL, type == 'C', narg );
            }
            else {
                IntrFuncCallEnd( 1UL, type == 'C', narg );
            }
        }
    }

    /* if we need a statement                                              */
    else if ( mode == 's' || (mode == 'x' && IS_IN(TLS->symbol, S_ASSIGN)) ) {
        if ( type != 'c' && type != 'C') {
	    if (TLS->symbol != S_ASSIGN)
	      Match( S_INCORPORATE, ":= or ::=", follow);
	    else
	      Match( S_ASSIGN, ":= or ::=", follow );
            if ( TLS->countNams == 0 || !TLS->intrCoding ) { TLS->currLHSGVar = (type == 'g' ? var : 0); }
            ReadExpr( follow, 'r' );
        }
        if ( READ_ERROR() ) {}
        else if ( type == 'l' ) { IntrAssLVar( var );             }
        else if ( type == 'h' ) { IntrAssHVar( var );             }
        else if ( type == 'd' ) { IntrAssDVar( var, nest0 - 1 );  }
        else if ( type == 'g' ) { IntrAssGVar( var );             }
        else if ( type == '[' ) { IntrAssList();                  }
        else if ( type == ']' ) { IntrAssListLevel( level );      }
        else if ( type == '{' ) { IntrAsssList();                 }
        else if ( type == '}' ) { IntrAsssListLevel( level );     }
        else if ( type == '<' ) { IntrAssPosObj();                }
        else if ( type == '>' ) { IntrAssPosObjLevel( level );    }
        else if ( type == '(' ) { IntrAsssPosObj();               }
        else if ( type == ')' ) { IntrAsssPosObjLevel( level );   }
        else if ( type == '.' ) { IntrAssRecName( rnam );         }
        else if ( type == ':' ) { IntrAssRecExpr();               }
        else if ( type == '!' ) { IntrAssComObjName( rnam );      }
        else if ( type == '|' ) { IntrAssComObjExpr();            }
        else if ( type == 'c' || type == 'C' )
	  { IntrFuncCallEnd( 0UL, type == 'C', narg ); }
    }

    /*  if we need an unbind                                               */
    else if ( mode == 'u' ) {
        if ( READ_ERROR() ) {}
        else if ( type == 'l' ) { IntrUnbLVar( var );             }
        else if ( type == 'h' ) { IntrUnbHVar( var );             }
        else if ( type == 'd' ) { IntrUnbDVar( var, nest0 - 1 );  }
        else if ( type == 'g' ) { IntrUnbGVar( var );             }
        else if ( type == '[' ) { IntrUnbList();                  }
        else if ( type == '<' ) { IntrUnbPosObj();                }
        else if ( type == '.' ) { IntrUnbRecName( rnam );         }
        else if ( type == ':' ) { IntrUnbRecExpr();               }
        else if ( type == '!' ) { IntrUnbComObjName( rnam );      }
        else if ( type == '|' ) { IntrUnbComObjExpr();            }
        else { SyntaxError("illegal operand for 'Unbind'");       }
    }


    /* if we need an isbound                                               */
    else /* if ( mode == 'i' ) */ {
        if ( READ_ERROR() ) {}
        else if ( type == 'l' ) { IntrIsbLVar( var );             }
        else if ( type == 'h' ) { IntrIsbHVar( var );             }
        else if ( type == 'd' ) { IntrIsbDVar( var, nest0 - 1 );  }
        else if ( type == 'g' ) { IntrIsbGVar( var );             }
        else if ( type == '[' ) { IntrIsbList();                  }
        else if ( type == '<' ) { IntrIsbPosObj();                }
        else if ( type == '.' ) { IntrIsbRecName( rnam );         }
        else if ( type == ':' ) { IntrIsbRecExpr();               }
        else if ( type == '!' ) { IntrIsbComObjName( rnam );      }
        else if ( type == '|' ) { IntrIsbComObjExpr();            }
        else { SyntaxError("illegal operand for 'IsBound'");      }
    }

}


/****************************************************************************
**
*F  ReadIsBound( <follow> ) . . . . . . . . . . .  read an isbound expression
**
**  'ReadIsBound' reads an isbound expression.  In  case of an error it skips
**  all symbols up to one contained in <follow>.
**
**  <Atom> := 'IsBound' '(' <Var> ')'
*/
void            ReadIsBound (
    TypSymbolSet        follow )
{
    Match( S_ISBOUND, "IsBound", follow );
    Match( S_LPAREN, "(", follow );
    ReadCallVarAss( S_RPAREN|follow, 'i' );
    Match( S_RPAREN, ")", follow );
}


/****************************************************************************
**
*F  ReadPerm( <follow> )  . . . . . . . . . . . . . . . .  read a permutation
**
**  'ReadPerm' reads a permutation.  In case of an error it skips all symbols
**  up to one contained in <follow>.
**
**  Note that the first expression has already been read.  The reason is that
**  until the first  expression has been  read and a  comma is found it could
**  also be a parenthesized expression.
**
**  <Perm> :=  ( <Expr> {, <Expr>} ) { ( <Expr> {, <Expr>} ) }
**
*/
void ReadPerm (
    TypSymbolSet        follow )
{
    volatile UInt       nrc;            /* number of cycles                */
    volatile UInt       nrx;            /* number of expressions in cycle  */

    /* read the first cycle (first expression has already been read)       */
    nrx = 1;
    while ( TLS->symbol == S_COMMA ) {
        Match( S_COMMA, ",", follow );
        ReadExpr( S_COMMA|S_RPAREN|follow, 'r' );
        nrx++;
    }
    Match( S_RPAREN, ")", follow );
    nrc = 1;
    if ( ! READ_ERROR() ) { IntrPermCycle( nrx, nrc ); }

    /* read the remaining cycles                                           */
    while ( TLS->symbol == S_LPAREN ) {
        Match( S_LPAREN, "(", follow );
        ReadExpr( S_COMMA|S_RPAREN|follow, 'r' );
        nrx = 1;
        while ( TLS->symbol == S_COMMA ) {
            Match( S_COMMA, ",", follow );
            ReadExpr( S_COMMA|S_RPAREN|follow, 'r' );
            nrx++;
        }
        Match( S_RPAREN, ")", follow );
        nrc++;
        if ( ! READ_ERROR() ) { IntrPermCycle( nrx, nrc ); }
    }

    /* that was the permutation                                            */
    if ( ! READ_ERROR() ) { IntrPerm( nrc ); }
}

/****************************************************************************
**
*F  ReadLongNumber( <follow> )  . . . . . . . . . . . . . . . read a long integer
**
**  A `long integer' here means one whose digits don't fit into `TLS->value',
**  see scanner.c.  This function copies repeatedly  digits from `TLS->value'
**  into a GAP string until the full integer is read.
**
*/

static UInt appendToString(Obj string, UInt len)
{
       UInt len1 = strlen(TLS->value);
       GROW_STRING(string, len+len1+1);
       memcpy(CHARS_STRING(string) + len, (void *)TLS->value, len1+1);
       SET_LEN_STRING(string, len+len1);
       return len + len1;
}

void ReadLongNumber(
      TypSymbolSet        follow )
{
     Obj  string;
     UInt len;
     UInt status;
     UInt done;

     /* string in which to accumulate number */
     len = strlen(TLS->value);
     string = NEW_STRING(len);
     memcpy(CHARS_STRING(string), (void *)TLS->value, len+1);
     done = 0;

     while (!done) {
       /* remember the current symbol and get the next one */
       status = TLS->symbol;
       Match(TLS->symbol, "partial number", follow);

       /* Now there are just lots of cases */
       switch (status) {
       case S_PARTIALINT:
	 switch (TLS->symbol) {
	 case S_INT:
	   len = appendToString(string, len);
	   Match(S_INT, "integer", follow);
	   IntrLongIntExpr(string);
	   done = 1;
	   break;

	 case S_PARTIALINT:
	   len = appendToString(string, len);
	   /*	   Match(S_PARTIALINT, "integer", follow);*/
	   break;

	 case S_PARTIALFLOAT1:
	   assert(0);
	   Pr("Parsing error, this should never happen", 0L, 0L);
	   SyExit(2);

	 case S_PARTIALFLOAT2:
	 case S_PARTIALFLOAT3:
	 case S_PARTIALFLOAT4:
	   status = TLS->symbol;
	   len = appendToString(string, len);
	   /* Match(TLS->symbol, "float", follow); */
	   break;

	 case S_FLOAT:
	   len = appendToString(string, len);
	   Match(S_FLOAT, "float", follow);
	   IntrLongFloatExpr(string);
	   done = 1;
	   break;

	 case S_IDENT:
	   SyntaxError("Identifier over 1024 characters");

	 default:
	   len = appendToString(string, len);
	   IntrLongIntExpr(string);
	   done = 1;
	 }
	 break;

       case S_PARTIALFLOAT1:
	 switch (TLS->symbol) {
	 case S_INT:
	 case S_PARTIALINT:
	 case S_PARTIALFLOAT1:
	   assert(0);
	   Pr("Parsing error, this should never happen", 0L, 0L);
	   SyExit(2);


	 case S_PARTIALFLOAT2:
	 case S_PARTIALFLOAT3:
	 case S_PARTIALFLOAT4:
	   status = TLS->symbol;
	   len = appendToString(string, len);
	   /* Match(TLS->symbol, "float", follow); */
	   break;

	 case S_FLOAT:
	   len = appendToString(string, len);
	   Match(S_FLOAT, "float", follow);
	   IntrLongFloatExpr(string);
	   done = 1;
	   break;

	 default:
	   SyntaxError("Badly Formed Number");
	 }
	 break;

       case S_PARTIALFLOAT2:
	 switch (TLS->symbol) {
	 case S_INT:
	 case S_PARTIALINT:
	 case S_PARTIALFLOAT1:
	   assert(0);
	   Pr("Parsing error, this should never happen", 0L, 0L);
	   SyExit(2);


	 case S_PARTIALFLOAT2:
	 case S_PARTIALFLOAT3:
	 case S_PARTIALFLOAT4:
	   status = TLS->symbol;
	   len = appendToString(string, len);
	   /* Match(TLS->symbol, "float", follow); */
	   break;

	 case S_FLOAT:
	   len = appendToString(string, len);
	   Match(S_FLOAT, "float", follow);
	   IntrLongFloatExpr(string);
	   done = 1;
	   break;


	 case S_IDENT:
	   SyntaxError("Badly Formed Number");

	 default:
	   len = appendToString(string, len);
	   IntrLongFloatExpr(string);
	   done = 1;
	 }
	 break;

       case S_PARTIALFLOAT3:
	 switch (TLS->symbol) {
	 case S_INT:
	 case S_PARTIALINT:
	 case S_PARTIALFLOAT1:
	 case S_PARTIALFLOAT2:
	 case S_PARTIALFLOAT3:
	   assert(0);
	   Pr("Parsing error, this should never happen", 0L, 0L);
	   SyExit(2);


	 case S_PARTIALFLOAT4:
	   status = TLS->symbol;
	   len = appendToString(string, len);
	   /* Match(TLS->symbol, "float", follow); */
	   break;

	 case S_FLOAT:
	   len = appendToString(string, len);
	   Match(S_FLOAT, "float", follow);
	   IntrLongFloatExpr(string);
	   done = 1;
	   break;


	 default:
	   SyntaxError("Badly Formed Number");

	 }
	 break;
       case S_PARTIALFLOAT4:
	 switch (TLS->symbol) {
	 case S_INT:
	 case S_PARTIALINT:
	 case S_PARTIALFLOAT1:
	 case S_PARTIALFLOAT2:
	 case S_PARTIALFLOAT3:
	   assert(0);
	   Pr("Parsing error, this should never happen", 0L, 0L);
	   SyExit(2);


	 case S_PARTIALFLOAT4:
	   status = TLS->symbol;
	   len = appendToString(string, len);
	   /* Match(TLS->symbol, "float", follow); */
	   break;

	 case S_FLOAT:
	   len = appendToString(string, len);
	   Match(S_FLOAT, "float", follow);
	   IntrLongFloatExpr(string);
	   done = 1;
	   break;

	 case S_IDENT:
	   SyntaxError("Badly Formed Number");

	 default:
	   len = appendToString(string, len);
	   IntrLongFloatExpr(string);
	   done = 1;

	 }
	 break;
       default:
	 assert(0);
	 Pr("Parsing error, this should never happen", 0L, 0L);
	 SyExit(2);
       }
     }
}

/****************************************************************************
**
*F  ReadString( <follow> )  . . . . . . . . . . . . . . read a (long) string
**
**  A string is  read by copying parts of `TLS->value'  (see scanner.c) given
**  by `TLS->valueLen' into  a string GAP object. This is  repeated until the
**  end of the string is reached.
**
*/
void ReadString(
      TypSymbolSet        follow )
{
     Obj  string;
     UInt len;

     string = NEW_STRING(TLS->valueLen);
     len = TLS->valueLen;
     memcpy(CHARS_STRING(string), (void *)TLS->value, TLS->valueLen);

     while (TLS->symbol == S_PARTIALSTRING) {
         Match(S_PARTIALSTRING, "", follow);
         GROW_STRING(string, len + TLS->valueLen);
         memcpy(CHARS_STRING(string) + len, (void *)TLS->value,
                                        TLS->valueLen);
         len += TLS->valueLen;
     }

     Match(S_STRING, "", follow);
     SET_LEN_STRING(string, len);
     /* ensure trailing zero for interpretation as C-string */
     *(CHARS_STRING(string) + len) = 0;
     IntrStringExpr( string );
}

/****************************************************************************
**
*F  ReadListExpr( <follow> )  . . . . . . . . . . . . . . . . . . read a list
**
**  'ReadListExpr'  reads a list literal expression.   In case of an error it
**  skips all symbols up to one contained in <follow>.
**
**  <List> := '[' [ <Expr> ] {',' [ <Expr> ] } ']'
**         |  '[' <Expr> [',' <Expr>] '..' <Expr> ']'
*/
void ReadListExpr (
    TypSymbolSet        follow )
{
    volatile UInt       pos;            /* actual position of element      */
    volatile UInt       nr;             /* number of elements              */
    volatile UInt       range;          /* is the list expression a range  */

    /* '['                                                                 */
    Match( S_LBRACK, "[", follow );
    TLS->readTop++;
    if ( TLS->readTop == 1 ) { TLS->readTilde = 0; }
    if ( ! READ_ERROR() ) { IntrListExprBegin( (TLS->readTop == 1) ); }
    pos   = 1;
    nr    = 0;
    range = 0;

    /* [ <Expr> ]                                                          */
    if ( TLS->symbol != S_COMMA && TLS->symbol != S_RBRACK ) {
        if ( ! READ_ERROR() ) { IntrListExprBeginElm( pos ); }
        ReadExpr( S_RBRACK|follow, 'r' );
        if ( ! READ_ERROR() ) { IntrListExprEndElm(); }
        nr++;
    }

    /* {',' [ <Expr> ] }                                                   */
    while ( TLS->symbol == S_COMMA ) {
        Match( S_COMMA, ",", follow );
        pos++;
        if ( TLS->symbol != S_COMMA && TLS->symbol != S_RBRACK ) {
            if ( ! READ_ERROR() ) { IntrListExprBeginElm( pos ); }
            ReadExpr( S_RBRACK|follow, 'r' );
            if ( ! READ_ERROR() ) { IntrListExprEndElm(); }
            nr++;
        }
    }

    /* '..' <Expr> ']'                                                     */
    if ( TLS->symbol == S_DOTDOT ) {
        if ( pos != nr ) {
            SyntaxError("must have no unbound entries in range");
        }
        if ( 2 < nr ) {
            SyntaxError("must have at most 2 entries before '..'");
        }
        range = 1;
        Match( S_DOTDOT, "..", follow );
        pos++;
        if ( ! READ_ERROR() ) { IntrListExprBeginElm( pos ); }
        ReadExpr( S_RBRACK|follow, 'r' );
        if ( ! READ_ERROR() ) { IntrListExprEndElm(); }
        nr++;
        if ( TLS->readTop == 1 && TLS->readTilde == 1 ) {
            SyntaxError("sorry, '~' not allowed in range");
        }
    }

    /* ']'                                                                 */
    Match( S_RBRACK, "]", follow );
    if ( ! READ_ERROR() ) {
        IntrListExprEnd( nr, range, (TLS->readTop == 1), (TLS->readTilde == 1) );
    }
    if ( TLS->readTop == 1 ) { TLS->readTilde = 0; }
    TLS->readTop--;
}


/****************************************************************************
**
*F  ReadRecExpr( <follow> ) . . . . . . . . . . . . . . . . . . read a record
**
**  'ReadRecExpr' reads a record literal expression.  In  case of an error it
**  skips all symbols up to one contained in <follow>.
**
**  <Record> := 'rec( [ <Ident>:=<Expr> {, <Ident>:=<Expr> } ] )'
*/
void ReadRecExpr (
    TypSymbolSet        follow )
{
    volatile UInt       rnam;           /* record component name           */
    volatile UInt       nr;             /* number of components            */

    /* 'rec('                                                              */
    Match( S_REC, "rec", follow );
    Match( S_LPAREN, "(", follow|S_RPAREN|S_COMMA );
    TLS->readTop++;
    if ( TLS->readTop == 1 ) { TLS->readTilde = 0; }
    if ( ! READ_ERROR() ) { IntrRecExprBegin( (TLS->readTop == 1) ); }
    nr = 0;

    /* [ <Ident> | '(' <Expr> ')' ':=' <Expr>                              */
    do {
      if (nr || TLS->symbol == S_COMMA) {
	Match(S_COMMA, ",", follow);
      }
      if ( TLS->symbol != S_RPAREN ) {
        if ( TLS->symbol == S_INT ) {
	  rnam = RNamName( TLS->value );
	  Match( S_INT, "integer", follow );
	  if ( ! READ_ERROR() ) { IntrRecExprBeginElmName( rnam ); }
        }
        else if ( TLS->symbol == S_IDENT ) {
	  rnam = RNamName( TLS->value );
	  Match( S_IDENT, "identifier", follow );
	  if ( ! READ_ERROR() ) { IntrRecExprBeginElmName( rnam ); }
        }
        else if ( TLS->symbol == S_LPAREN ) {
	  Match( S_LPAREN, "(", follow );
	  ReadExpr( follow, 'r' );
	  Match( S_RPAREN, ")", follow );
	  if ( ! READ_ERROR() ) { IntrRecExprBeginElmExpr(); }
        }
        else {
	  SyntaxError("identifier expected");
        }
        Match( S_ASSIGN, ":=", follow );
        ReadExpr( S_RPAREN|follow, 'r' );
        if ( ! READ_ERROR() ) { IntrRecExprEndElm(); }
        nr++;
      }

    }
  while ( TLS->symbol == S_COMMA );

    /* ')'                                                                 */
    Match( S_RPAREN, ")", follow );
    if ( ! READ_ERROR() ) {
        IntrRecExprEnd( nr, (TLS->readTop == 1), (TLS->readTilde == 1) );
    }
    if ( TLS->readTop == 1) { TLS->readTilde = 0; }
    TLS->readTop--;
}


/****************************************************************************
**
*F  ReadFuncExpr( <follow> )  . . . . . . . . . .  read a function definition
**
**  'ReadFuncExpr' reads a function literal expression.  In  case of an error
**  it skips all symbols up to one contained in <follow>.
**
**  <Function> := 'function (' [ <Ident> {',' <Ident>} ] ')'
**                             [ 'local'  <Ident> {',' <Ident>} ';' ]
**                             <Statments>
**                'end'
*/


void ReadFuncExpr (
    TypSymbolSet        follow )
{
    volatile Obj        nams;           /* list of local variables names   */
    volatile Obj        name;           /* one local variable name         */
    volatile UInt       narg;           /* number of arguments             */
    volatile UInt       nloc;           /* number of locals                */
    volatile UInt       nr;             /* number of statements            */
    volatile UInt       i;              /* loop variable                   */
    volatile UInt       nrError;        /* copy of <TLS->nrError>          */
    volatile Bag        currLVars;      /* copy of <TLS->currLVars>             */
    volatile Int        startLine;      /* line number of function keyword */
    volatile int        is_block = 0;   /* is this a do ... od block?      */
    volatile int        is_atomic = 0;  /* is this an atomic function?      */
    volatile int        lockmode;       /* type of lock for current argument */
    volatile Bag        locks = 0;      /* locks of the function */

    /* begin the function               */
    startLine = TLS->input->number;
    if (TLS->symbol == S_DO) {
	Match( S_DO, "do", follow );
        is_block = 1;
    } else {
	if (TLS->symbol == S_ATOMIC) {
	    Match(S_ATOMIC, "atomic", follow);
	    is_atomic = 1;
	    locks = NEW_STRING(4);
	}
	Match( S_FUNCTION, "function", follow );
	Match( S_LPAREN, "(", S_IDENT|S_RPAREN|S_LOCAL|STATBEGIN|S_END|follow );
    }

    /* make and push the new local variables list (args and locals)        */
    narg = nloc = 0;
    nams = NEW_PLIST( T_PLIST, narg+nloc );
    SET_LEN_PLIST( nams, narg+nloc );
    TLS->countNams += 1;
    ASS_LIST( TLS->stackNams, TLS->countNams, nams );
    if (!is_block) {
	if ( TLS->symbol != S_RPAREN ) {
	    lockmode = 0;
	    switch (TLS->symbol) {
	      case S_READWRITE:
	        if (!is_atomic)
		  SyntaxError("'readwrite' argument of non-atomic function");
	        lockmode++;
	      case S_READONLY:
	        if (!is_atomic)
		  SyntaxError("'readonly' argument of non-atomic function");
	        lockmode++;
		CHARS_STRING(locks)[0] = lockmode;
		SET_LEN_STRING(locks, 1);
	        GetSymbol();
	    }
	    name = NEW_STRING( strlen(TLS->value) );
	    SyStrncat( CSTR_STRING(name), TLS->value, strlen(TLS->value) );
	    MakeImmutableString(name);
	    narg += 1;
	    ASS_LIST( nams, narg+nloc, name );
	    Match(S_IDENT,"identifier",S_RPAREN|S_LOCAL|STATBEGIN|S_END|follow);
	}
	while ( TLS->symbol == S_COMMA ) {
	    Match( S_COMMA, ",", follow );
	    lockmode = 0;
	    switch (TLS->symbol) {
	      case S_READWRITE:
	        if (!is_atomic)
		  SyntaxError("'readwrite' argument of non-atomic function");
	        lockmode++;
	      case S_READONLY:
	        if (!is_atomic)
		  SyntaxError("'readonly' argument of non-atomic function");
	        lockmode++;
		GrowString(locks, narg+1);
		SET_LEN_STRING(locks, narg+1);
		CHARS_STRING(locks)[narg] = lockmode;
	        GetSymbol();
	    }
	    for ( i = 1; i <= narg; i++ ) {
		if ( strcmp(CSTR_STRING(ELM_LIST(nams,i)),TLS->value) == 0 ) {
		    SyntaxError("name used for two arguments");
		}
	    }
	    name = NEW_STRING( strlen(TLS->value) );
	    SyStrncat( CSTR_STRING(name), TLS->value, strlen(TLS->value) );
	    MakeImmutableString(name);
	    narg += 1;
	    ASS_LIST( nams, narg+nloc, name );
	    Match(S_IDENT,"identifier",S_RPAREN|S_LOCAL|STATBEGIN|S_END|follow);
	}
        Match( S_RPAREN, ")", S_LOCAL|STATBEGIN|S_END|follow );
    }
    if ( TLS->symbol == S_LOCAL ) {
        Match( S_LOCAL, "local", follow );
        for ( i = 1; i <= narg; i++ ) {
            if ( strcmp(CSTR_STRING(ELM_LIST(nams,i)),TLS->value) == 0 ) {
                SyntaxError("name used for argument and local");
            }
        }
        name = NEW_STRING( strlen(TLS->value) );
        SyStrncat( CSTR_STRING(name), TLS->value, strlen(TLS->value) );
	MakeImmutableString(name);
        nloc += 1;
        ASS_LIST( nams, narg+nloc, name );
        Match( S_IDENT, "identifier", STATBEGIN|S_END|follow );
        while ( TLS->symbol == S_COMMA ) {
            /* init to avoid strange message in case of empty string */
            TLS->value[0] = '\0';
            Match( S_COMMA, ",", follow );
            for ( i = 1; i <= narg; i++ ) {
                if ( strcmp(CSTR_STRING(ELM_LIST(nams,i)),TLS->value) == 0 ) {
                    SyntaxError("name used for argument and local");
                }
            }
            for ( i = narg+1; i <= narg+nloc; i++ ) {
                if ( strcmp(CSTR_STRING(ELM_LIST(nams,i)),TLS->value) == 0 ) {
                    SyntaxError("name used for two locals");
                }
            }
            name = NEW_STRING( strlen(TLS->value) );
            SyStrncat( CSTR_STRING(name), TLS->value, strlen(TLS->value) );
	    MakeImmutableString(name);
            nloc += 1;
            ASS_LIST( nams, narg+nloc, name );
            Match( S_IDENT, "identifier", STATBEGIN|S_END|follow );
        }
        Match( S_SEMICOLON, ";", STATBEGIN|S_END|follow );
    }

    /* 'function( arg )' takes a variable number of arguments              */
    if ( narg == 1 && ! strcmp( "arg", CSTR_STRING( ELM_LIST(nams,1) ) ) )
        narg = -1;

    /* remember the current variables in case of an error                  */
    currLVars = TLS->currLVars;
    nrError   = TLS->nrError;

    /* now finally begin the function                                      */
    if ( ! READ_ERROR() ) { IntrFuncExprBegin( narg, nloc, nams, startLine ); }
    if ( nrError == 0) LCKS_FUNC(CURR_FUNC) = locks;

    /* <Statments>                                                         */
    nr = ReadStats( S_END|follow );

    /* and end the function again                                          */
    if ( ! READ_ERROR() ) {
        IntrFuncExprEnd( nr, 0UL );
    }

    /* an error has occured *after* the 'IntrFuncExprEnd'                  */
    else if ( nrError == 0 && TLS->intrCoding ) {
        CodeEnd(1);
        TLS->intrCoding--;
        TLS->currLVars = currLVars;
        TLS->ptrLVars  = PTR_BAG( TLS->currLVars );
        TLS->ptrBody   = (Stat*) PTR_BAG( BODY_FUNC( CURR_FUNC ) );
    }

    /* pop the new local variables list                                    */
    assert(TLS->countNams > 0);
    TLS->countNams--;

    /* 'end'                                                               */
    if (is_block)
        Match(S_OD, "od", follow );
    else
        Match( S_END, "end", follow );
}


/****************************************************************************
**
*F  ReadFuncExpr1(<follow>) . . . . . . . . . . .  read a function expression
**
**  'ReadFuncExpr1' reads  an abbreviated  function literal   expression.  In
**  case of an error it skips all symbols up to one contained in <follow>.
**
**      <Function>      := <Var> '->' <Expr>
*/
void ReadFuncExpr1 (
    TypSymbolSet        follow )
{
    volatile Obj        nams;           /* list of local variables names   */
    volatile Obj        name;           /* one local variable name         */
    volatile UInt       nrError;        /* copy of <TLS->nrError>          */
    volatile Bag        currLVars;      /* copy of <TLS->currLVars>             */

    /* make and push the new local variables list                          */
    nams = NEW_PLIST( T_PLIST, 1 );
    SET_LEN_PLIST( nams, 0 );
    TLS->countNams++;
    ASS_LIST( TLS->stackNams, TLS->countNams, nams );
    name = NEW_STRING( strlen(TLS->value) );
    SyStrncat( CSTR_STRING(name), TLS->value, strlen(TLS->value) );
    MakeImmutableString( name );
    ASS_LIST( nams, 1, name );

    /* match away the '->'                                                 */
    Match( S_MAPTO, "->", follow );

    /* remember the current variables in case of an error                  */
    currLVars = TLS->currLVars;
    nrError   = TLS->nrError;

    /* begin interpreting the function expression (with 1 argument)        */
    if ( ! READ_ERROR() ) { IntrFuncExprBegin( 1L, 0L, nams, TLS->input->number ); }

    /* read the expression and turn it into a return-statement             */
    ReadExpr( follow, 'r' );
    if ( ! READ_ERROR() ) { IntrReturnObj(); }

    /* end interpreting the function expression (with 1 statement)         */
    if ( ! READ_ERROR() ) {
        IntrFuncExprEnd( 1UL, 1UL );
    }

    /* an error has occured *after* the 'IntrFuncExprEnd'                  */
    else if ( nrError == 0  && TLS->intrCoding ) {
        CodeEnd(1);
        TLS->intrCoding--;
        TLS->currLVars = currLVars;
        TLS->ptrLVars  = PTR_BAG( TLS->currLVars );
        TLS->ptrBody   = (Stat*) PTR_BAG( BODY_FUNC( CURR_FUNC ) );
    }

    /* pop the new local variables list                                    */
    assert(TLS->countNams > 0);
    TLS->countNams--;
}


/****************************************************************************
**
*F  ReadFuncExpr0(<follow>) . . . . . . . . . . .  read a function expression
**
**  'ReadFuncExpr0' reads  an abbreviated  function literal   expression.  In
**  case of an error it skips all symbols up to one contained in <follow>.
**
**      <Function>      := '->' <Expr>
*/
void ReadFuncExpr0 (
    TypSymbolSet        follow )
{
    volatile Obj        nams;           /* list of local variables names   */
    volatile UInt       nrError;        /* copy of <TLS->nrError>          */
    volatile Bag        currLVars;      /* copy of <TLS->currLVars>             */

    /* make and push the new local variables list                          */
    nams = NEW_PLIST( T_PLIST, 0 );
    SET_LEN_PLIST( nams, 0 );
    TLS->countNams++;
    ASS_LIST( TLS->stackNams, TLS->countNams, nams );

    /* match away the '->'                                                 */
    Match( S_MAPTO, "->", follow );

    /* remember the current variables in case of an error                  */
    currLVars = TLS->currLVars;
    nrError   = TLS->nrError;

    /* begin interpreting the function expression (with 1 argument)        */
    if ( ! READ_ERROR() ) { IntrFuncExprBegin( 0L, 0L, nams, TLS->input->number ); }

    /* read the expression and turn it into a return-statement             */
    ReadExpr( follow, 'r' );
    if ( ! READ_ERROR() ) { IntrReturnObj(); }

    /* end interpreting the function expression (with 1 statement)         */
    if ( ! READ_ERROR() ) {
        IntrFuncExprEnd( 1UL, 1UL );
    }

    /* an error has occured *after* the 'IntrFuncExprEnd'                  */
    else if ( nrError == 0  && TLS->intrCoding ) {
        CodeEnd(1);
        TLS->intrCoding--;
        TLS->currLVars = currLVars;
        TLS->ptrLVars  = PTR_BAG( TLS->currLVars );
        TLS->ptrBody   = (Stat*) PTR_BAG( BODY_FUNC( CURR_FUNC ) );
    }

    /* pop the new local variables list                                    */
    TLS->countNams--;
}

/****************************************************************************
**
*F  ReadLiteral( <follow> ) . . . . . . . . . . . . . . . . . .  read an atom
**
**  'ReadLiteral' reads a  literal expression.  In  case of an error it skips
**  all symbols up to one contained in <follow>.
**
**  <Literal> := <Int>
**            |  'true'
**            |  'false'
**            |  <Char>
**            |  <Perm>
**            |  <String>
**            |  <List>
**            |  <Record>
**            |  <Function>
**
**  <Int>     := 0|1|..|9 { 0|1|..|9 }
**
**  <Char>    := ' <any character> '
**
**  <String>  := " { <any character> } "
*/
void ReadLiteral (
    TypSymbolSet        follow )
{
    /* <Int>                                                               */
    if ( TLS->symbol == S_INT ) {
        if ( ! READ_ERROR() ) { IntrIntExpr( TLS->value ); }
        Match( S_INT, "integer", follow );
    }

    /* <Float> */
    else if ( TLS->symbol == S_FLOAT ) {
        if ( ! READ_ERROR() ) { IntrFloatExpr( TLS->value ); }
        Match( S_FLOAT, "float", follow );
    }


    /* partial Int */
    else if ( TLS->symbol == S_PARTIALINT || TLS->symbol == S_PARTIALFLOAT1 ||
	      TLS->symbol == S_PARTIALFLOAT2 ) {
         ReadLongNumber( follow );
    }

    /* 'true'                                                              */
    else if ( TLS->symbol == S_TRUE ) {
        Match( S_TRUE, "true", follow );
        IntrTrueExpr();
    }


    /* 'false'                                                             */
    else if ( TLS->symbol == S_FALSE ) {
        Match( S_FALSE, "false", follow );
        IntrFalseExpr();
    }

    /* <Char>                                                              */
    else if ( TLS->symbol == S_CHAR ) {
        if ( ! READ_ERROR() ) { IntrCharExpr( TLS->value[0] ); }
        Match( S_CHAR, "character", follow );
    }

    /* (partial) string */
    else if ( TLS->symbol == S_STRING || TLS->symbol == S_PARTIALSTRING ) {
        ReadString( follow );
    }

    /* <List>                                                              */
    else if ( TLS->symbol == S_LBRACK ) {
        ReadListExpr( follow );
    }

    /* <Rec>                                                               */
    else if ( TLS->symbol == S_REC ) {
        ReadRecExpr( follow );
    }
    /* `Literal								   */
    else if ( TLS->symbol == S_BACKQUOTE ) {
        Match( S_BACKQUOTE, "`", follow );
	if (!READ_ERROR()) {
	  IntrRefGVar(GVarName("MakeLiteral"));
	  IntrFuncCallBegin();
	  ReadAtom( follow, 'r' );
	  IntrFuncCallEnd(1, 0, 1);
	}
    }


    /* <Function>                                                          */
    else if ( TLS->symbol == S_FUNCTION || TLS->symbol == S_ATOMIC ||
              TLS->symbol == S_DO) {
        ReadFuncExpr( follow );
    }

    else if (TLS->symbol == S_DOT ) {
      /* Hack The only way a dot could turn up here is in
       a floating point literal that starts with .. So, change the token
      to  a partial float of the right kind to end with a . and an
      associated value and dive into the long float literal handler in the parser*/
      TLS->symbol = S_PARTIALFLOAT1;
      TLS->value[0] = '.';
      TLS->value[1] = '\0';
      ReadLongNumber( follow );
    }
    else if (TLS->symbol == S_MAPTO) {
      ReadFuncExpr0( follow );
    }

    /* signal an error, we want to see a literal                           */
    else {
        Match( S_INT, "literal", follow );
    }

}

/****************************************************************************
**
*F  ReadAtom( <follow>, <mode> )  . . . . . . . . . . . . . . .  read an atom
**
**  'ReadAtom' reads an atom.  In case  of an error it skips  all symbols up to
**  one contained in <follow>.
**
**   <Atom> := <Var>
**          |  'IsBound' '(' <Var> ')'
**          |  <Literal>
**          |  '(' <Expr> ')'
*/
void ReadAtom (
    TypSymbolSet        follow,
    Char                mode )
{
    /* read a variable                                                     */
    if ( TLS->symbol == S_IDENT ) {
        ReadCallVarAss( follow, mode );
    }

    /* 'IsBound' '(' <Var> ')'                                             */
    else if ( TLS->symbol == S_ISBOUND ) {
        ReadIsBound( follow );
    }
    /* otherwise read a literal expression                                 */
    else if (IS_IN(TLS->symbol,S_INT|S_TRUE|S_FALSE|S_CHAR|S_STRING|S_LBRACK|
                          S_REC|S_FUNCTION|S_DO|S_ATOMIC| S_FLOAT | S_DOT |
			  S_MAPTO))
    {
        ReadLiteral( follow );
    }

    /* '(' <Expr> ')'                                                      */
    else if ( TLS->symbol == S_LPAREN ) {
        Match( S_LPAREN, "(", follow );
        if ( TLS->symbol == S_RPAREN ) {
            Match( S_RPAREN, ")", follow );
            if ( ! READ_ERROR() ) { IntrPerm( 0UL ); }
            return;
        }
        ReadExpr( S_RPAREN|follow, 'r' );
        if ( TLS->symbol == S_COMMA ) {
            ReadPerm( follow );
            return;
        }
        Match( S_RPAREN, ")", follow );
    }

    /* otherwise signal an error                                           */
    else {
        Match( S_INT, "expression", follow );
    }
}



/****************************************************************************
**
*F  ReadFactor( <follow>, <mode> )  . . . . . . . . . . . . . . read a factor
**
**  'ReadFactor' reads a factor.  In case of an error it skips all symbols up
**  to one contained in <follow>.
**
**  <Factor> := {'+'|'-'} <Atom> [ '^' {'+'|'-'} <Atom> ]
*/
void ReadFactor (
    TypSymbolSet        follow,
    Char                mode )
{
    volatile Int        sign1;
    volatile Int        sign2;

    /* { '+'|'-' }  leading sign                                           */
    sign1 = 0;
    if ( TLS->symbol == S_MINUS  || TLS->symbol == S_PLUS ) {
        if ( sign1 == 0 )  sign1 = 1;
        if ( TLS->symbol == S_MINUS ) { sign1 = -sign1; }
        Match( TLS->symbol, "unary + or -", follow );
    }

    /* <Atom>                                                              */
    ReadAtom( follow, (sign1 == 0 ? mode : 'r') );

    /* ['^' <Atom> ] implemented as {'^' <Atom> } for better error message */
    while ( TLS->symbol == S_POW ) {

        /* match the '^' away                                              */
        Match( S_POW, "^", follow );

        /* { '+'|'-' }  leading sign                                       */
        sign2 = 0;
        if ( TLS->symbol == S_MINUS  || TLS->symbol == S_PLUS ) {
            if ( sign2 == 0 )  sign2 = 1;
            if ( TLS->symbol == S_MINUS ) { sign2 = -sign2; }
            Match( TLS->symbol, "unary + or -", follow );
        }

        /* ['^' <Atom>]                                                    */
        ReadAtom( follow, 'r' );

        /* interpret the unary minus                                       */
        if ( sign2 == -1 && ! READ_ERROR() ) { IntrAInv(); }

        /* interpret the power                                             */
        if ( ! READ_ERROR() ) { IntrPow(); }

        /* check for multiple '^'                                          */
        if ( TLS->symbol == S_POW ) { SyntaxError("'^' is not associative"); }

    }

    /* interpret the unary minus                                           */
    if ( sign1 == -1 && ! READ_ERROR() ) { IntrAInv(); }
}


/****************************************************************************
**
*F  ReadTerm( <follow>, <mode> )  . . . . . . . . . . . . . . . . read a term
**
**  'ReadTerm' reads a term.  In case of an error it  skips all symbols up to
**  one contained in <follow>.
**
**  <Term> := <Factor> { '*'|'/'|'mod' <Factor> }
*/
void ReadTerm (
    TypSymbolSet        follow,
    Char                mode )
{
    volatile UInt       symbol;

    /* <Factor>                                                            */
    ReadFactor( follow, mode );

    /* { '*'|'/'|'mod' <Factor> }                                          */
    /* do not use 'IS_IN', since 'IS_IN(S_POW,S_MULT|S_DIV|S_MOD)' is true */
    while ( TLS->symbol == S_MULT || TLS->symbol == S_DIV || TLS->symbol == S_MOD ) {
        symbol = TLS->symbol;
        Match( TLS->symbol, "*, /, or mod", follow );
        ReadFactor( follow, 'r' );
        if ( ! READ_ERROR() ) {
            if      ( symbol == S_MULT ) { IntrProd(); }
            else if ( symbol == S_DIV  ) { IntrQuo();  }
            else if ( symbol == S_MOD  ) { IntrMod();  }
        }
    }
}


/****************************************************************************
**
*F  ReadAri( <follow>, <mode> ) . . . . . . . . read an arithmetic expression
**
**  'ReadAri' reads an  arithmetic expression.  In  case of an error it skips
**  all symbols up to one contained in <follow>.
**
**  <Arith> := <Term> { '+'|'-' <Term> }
*/
void ReadAri (
    TypSymbolSet        follow,
    Char                mode )
{
    UInt                symbol;

    /* <Term>                                                              */
    ReadTerm( follow, mode );

    /* { '+'|'-' <Term> }                                                  */
    while ( IS_IN( TLS->symbol, S_PLUS|S_MINUS ) ) {
        symbol = TLS->symbol;
        Match( TLS->symbol, "+ or -", follow );
        ReadTerm( follow, 'r' );
        if ( ! READ_ERROR() ) {
            if      ( symbol == S_PLUS  ) { IntrSum();  }
            else if ( symbol == S_MINUS ) { IntrDiff(); }
        }
    }
}


/****************************************************************************
**
*F  ReadRel( <follow>, <mode> ) . . . . . . . .  read a relational expression
**
**  'ReadRel' reads a relational  expression.  In case  of an error it  skips
**  all symbols up to one contained in <follow>.
**
**  <Rel> := { 'not' } <Arith> { '=|<>|<|>|<=|>=|in' <Arith> }
*/
void ReadRel (
    TypSymbolSet        follow,
    Char                mode )
{
    volatile UInt       symbol;
    volatile UInt       isNot;

    /* { 'not' }                                                           */
    isNot = 0;
    while ( TLS->symbol == S_NOT ) {
        isNot++;
        Match( S_NOT, "not", follow );
    }

    /* <Arith>                                                             */
    ReadAri( follow, (isNot == 0 ? mode : 'r') );

    /* { '=|<>|<|>|<=|>=|in' <Arith> }                                     */
    if ( IS_IN( TLS->symbol, S_EQ|S_LT|S_GT|S_NE|S_LE|S_GE|S_IN ) ) {
        symbol = TLS->symbol;
        Match( TLS->symbol, "comparison operator", follow );
        ReadAri( follow, 'r' );
        if ( ! READ_ERROR() ) {
            if      ( symbol == S_EQ ) { IntrEq(); }
            else if ( symbol == S_NE ) { IntrNe(); }
            else if ( symbol == S_LT ) { IntrLt(); }
            else if ( symbol == S_GE ) { IntrGe(); }
            else if ( symbol == S_GT ) { IntrGt(); }
            else if ( symbol == S_LE ) { IntrLe(); }
            else if ( symbol == S_IN ) { IntrIn(); }
        }
    }

    /* interpret the not                                                   */
    if ( (isNot % 2) != 0 ) {
        if ( ! READ_ERROR() ) { IntrNot(); }
    }
}


/****************************************************************************
**
*F  ReadAnd( <follow>, <mode> ) . . . . . . . read a logical 'and' expression
**
**  'ReadAnd' reads an and   expression.  In case of  an  error it  skips all
**  symbols up to one contained in <follow>.
**
**  <And> := <Rel> { 'and' <Rel> }
*/
void ReadAnd (
    TypSymbolSet        follow,
    Char                mode )
{
    /* <Rel>                                                               */
    ReadRel( follow, mode );

    /* { 'and' <Rel> }                                                     */
    while ( TLS->symbol == S_AND ) {
        Match( S_AND, "and", follow );
        if ( ! READ_ERROR() ) { IntrAndL(); }
        ReadRel( follow, 'r' );
        if ( ! READ_ERROR() ) { IntrAnd(); }
    }
}


/****************************************************************************
**
*F  ReadQualifiedExpr( <follow>, <mode> )  . . . . .  read an expression which
**                may be qualified with readonly or readwrite
**
**  'ReadQualifiedExpr' reads a qualifed expression.  In case of an error it skips all symbols
**  up to one contained in <follow>.
**
**  <QualifiedExpr> := ['readonly' | 'readwrite' ] <Expr>
*/
void ReadQualifiedExpr (
    TypSymbolSet        follow,
    Char                mode )
{
  UInt access  = 0;
  if (TLS->symbol == S_READWRITE) 
    {
      Match( S_READWRITE, "readwrite", follow | EXPRBEGIN );
      access = 2;
    }
  else if (TLS->symbol == S_READONLY) 
    {
      Match( S_READONLY, "readonly", follow | EXPRBEGIN );
      access = 1;
    }
  IntrQualifiedExprBegin(access);
  ReadExpr(follow,mode);
  IntrQualifiedExprEnd();
}



/****************************************************************************
**
*F  ReadExpr( <follow>, <mode> )  . . . . . . . . . . . .  read an expression
**
**  'ReadExpr' reads an expression.  In case of an error it skips all symbols
**  up to one contained in <follow>.
**
**  <Expr> := <And> { 'or' <And> }
*/
void ReadExpr (
    TypSymbolSet        follow,
    Char                mode )
{
    /* <And>                                                               */
    ReadAnd( follow, mode );

    /* { 'or' <And> }                                                      */
    while ( TLS->symbol == S_OR ) {
        Match( S_OR, "or", follow );
        if ( ! READ_ERROR() ) { IntrOrL(); }
        ReadAnd( follow, 'r' );
        if ( ! READ_ERROR() ) { IntrOr(); }
    }
}


/****************************************************************************
**
*F  ReadUnbind( <follow> )  . . . . . . . . . . . .  read an unbind statement
**
**  'ReadUnbind' reads an unbind statement.  In case of an error it skips all
**  symbols up to one contained in <follow>.
**
**  <Statment> := 'Unbind' '(' <Var> ')' ';'
*/
void ReadUnbind (
    TypSymbolSet        follow )
{
    Match( S_UNBIND, "Unbind", follow );
    Match( S_LPAREN, "(", follow );
    ReadCallVarAss( S_RPAREN|follow, 'u' );
    Match( S_RPAREN, ")", follow );
}


/****************************************************************************
**
*F  ReadEmpty( <follow> )  . . . . . . . . . . . . . .read an empty statement
**
**  'ReadEmpty' reads  an empty statement.  The argument is actually ignored
**
**  <Statment> :=  ';'
*/
void ReadEmpty (
    TypSymbolSet        follow )
{
  IntrEmpty();
}

/****************************************************************************
**
*F  ReadInfo( <follow> )  . . . . . . . . . . . . . .  read an info statement
**
**  'ReadInfo' reads  an info statement.  In  case of an  error  it skips all
**  symbols up to one contained in <follow>.
**
**  <Statment> := 'Info' '(' <Expr> ',' <Expr> { ',' <Expr> } ')' ';'
*/
void ReadInfo (
    TypSymbolSet        follow )
{
    volatile UInt       narg;     /* numer of arguments to print (or not)  */

    if ( ! READ_ERROR() ) { IntrInfoBegin(); }
    Match( S_INFO, "Info", follow );
    Match( S_LPAREN, "(", follow );
    ReadExpr( S_RPAREN | S_COMMA | follow, 'r');
    Match( S_COMMA, ",", S_RPAREN|follow);
    ReadExpr( S_RPAREN | S_COMMA | follow, 'r');
    if ( ! READ_ERROR() ) { IntrInfoMiddle(); }
    narg = 0;
    while ( TLS->symbol == S_COMMA ) {
        narg++;
        Match( S_COMMA, "", 0L);
        ReadExpr( S_RPAREN | S_COMMA | follow, 'r');
    }
    Match( S_RPAREN, ")", follow );
    if ( ! READ_ERROR() ) { IntrInfoEnd(narg); }
}


/****************************************************************************
**
*F  ReadAssert( <follow> )  . . . . . . . . . . . . .read an assert statement
**
**  'ReadAssert' reads an assert statement.  In case of an error it skips all
**  symbols up to one contained in <follow>.
**
**  <Statment> := 'Assert' '(' <Expr> ',' <Expr> [ ',' <Expr> ]  ')' ';'
*/
void ReadAssert (
    TypSymbolSet        follow )
{
    if ( ! READ_ERROR() ) { IntrAssertBegin(); }
    Match( S_ASSERT, "Assert", follow );
    Match( S_LPAREN, "(", follow );
    ReadExpr( S_RPAREN | S_COMMA | follow, 'r' );
    if ( ! READ_ERROR() ) { IntrAssertAfterLevel(); }
    Match( S_COMMA, ",", S_RPAREN|follow );
    ReadExpr( S_RPAREN | S_COMMA | follow, 'r' );
    if ( ! READ_ERROR() ) { IntrAssertAfterCondition(); }
    if ( TLS->symbol == S_COMMA )
      {
        Match( S_COMMA, "", 0L);
        ReadExpr( S_RPAREN |  follow, 'r' );
        Match( S_RPAREN, ")", follow );
        if ( ! READ_ERROR() ) { IntrAssertEnd3Args(); }
      }
    else
      {
	Match( S_RPAREN, ")", follow );
	if ( ! READ_ERROR() ) { IntrAssertEnd2Args(); }
      }
}

/****************************************************************************
**
*F  ReadIf( <follow> )  . . . . . . . . . . . . . . . .  read an if statement
**
**  'ReadIf' reads an if-statement.  In case of an error it skips all symbols
**  up to one contained in <follow>.
**
**  <Statement> := 'if'   <Expr> 'then' <Statments>
**                 { 'elif' <Expr> 'then' <Statments> }
**                 [ 'else'               <Statments> ]
**                 'fi' ';'
*/
void ReadIf (
    TypSymbolSet        follow )
{
    volatile UInt       nrb;            /* number of branches              */
    volatile UInt       nrs;            /* number of statements in a body  */

    /* 'if' <Expr>  'then' <Statments>                                     */
    nrb = 0;
    if ( ! READ_ERROR() ) { IntrIfBegin(); }
    Match( S_IF, "if", follow );
    ReadExpr( S_THEN|S_ELIF|S_ELSE|S_FI|follow, 'r' );
    Match( S_THEN, "then", STATBEGIN|S_ELIF|S_ELSE|S_FI|follow );
    if ( ! READ_ERROR() ) { IntrIfBeginBody(); }
    nrs = ReadStats( S_ELIF|S_ELSE|S_FI|follow );
    if ( ! READ_ERROR() ) { IntrIfEndBody( nrs ); }
    nrb++;

    /* { 'elif' <Expr>  'then' <Statments> }                               */
    while ( TLS->symbol == S_ELIF ) {
        if ( ! READ_ERROR() ) { IntrIfElif(); }
        Match( S_ELIF, "elif", follow );
        ReadExpr( S_THEN|S_ELIF|S_ELSE|S_FI|follow, 'r' );
        Match( S_THEN, "then", STATBEGIN|S_ELIF|S_ELSE|S_FI|follow );
        if ( ! READ_ERROR() ) { IntrIfBeginBody(); }
        nrs = ReadStats( S_ELIF|S_ELSE|S_FI|follow );
        if ( ! READ_ERROR() ) { IntrIfEndBody( nrs ); }
        nrb++;
    }

    /* [ 'else' <Statments> ]                                              */
    if ( TLS->symbol == S_ELSE ) {
        if ( ! READ_ERROR() ) { IntrIfElse(); }
        Match( S_ELSE, "else", follow );
        if ( ! READ_ERROR() ) { IntrIfBeginBody(); }
        nrs = ReadStats( S_FI|follow );
        if ( ! READ_ERROR() ) { IntrIfEndBody( nrs ); }
        nrb++;
    }

    /* 'fi'                                                                */
    Match( S_FI, "fi", follow );
    if ( ! READ_ERROR() ) { IntrIfEnd( nrb ); }
}


/****************************************************************************
**
*F  ReadFor( <follow> ) . . . . . . . . . . . . . . . .  read a for statement
**
**  'ReadFor' reads a for-loop.  In case of an error it  skips all symbols up
**  to one contained in <follow>.
**
**  <Statement> := 'for' <Var>  'in' <Expr>  'do'
**                     <Statments>
**                 'od' ';'
*/


void ReadFor (
    TypSymbolSet        follow )
{
    volatile UInt       nrs;            /* number of statements in body    */
    volatile UInt       nrError;        /* copy of <Tls->nrError>          */
    volatile Bag        currLVars;      /* copy of <TLS->currLVars>             */

    /* remember the current variables in case of an error                  */
    currLVars = TLS->currLVars;
    nrError   = TLS->nrError;

    /* 'for'                                                               */
    if ( ! READ_ERROR() ) { IntrForBegin(); }
    Match( S_FOR, "for", follow );

    /* <Var>                                                               */
    ReadCallVarAss( follow, 'r' );

    /* 'in' <Expr>                                                         */
    Match( S_IN, "in", S_DO|S_OD|follow );
    if ( ! READ_ERROR() ) { IntrForIn(); }
    ReadExpr( S_DO|S_OD|follow, 'r' );

    /* 'do' <Statments>                                                    */
    Match( S_DO, "do", STATBEGIN|S_OD|follow );
    if ( ! READ_ERROR() ) { IntrForBeginBody(); }
    nrs = ReadStats( S_OD|follow );
    if ( ! READ_ERROR() ) { IntrForEndBody( nrs ); }

    /* 'od'                                                                */
    Match( S_OD, "od", follow );
    if ( ! READ_ERROR() ) {
        IntrForEnd();
    }

    /* an error has occured *after* the 'IntrForEndBody'                  */
    /* If we hadn't actually come out of coding the body, we need
       to recover. Otherwise it was probably an error in executing the body and
       we just return */
    else if ( nrError == 0  && TLS->intrCoding ) {
      CodeEnd(1);
      TLS->intrCoding--;
      TLS->currLVars = currLVars;
      TLS->ptrLVars  = PTR_BAG( TLS->currLVars );
      TLS->ptrBody   = (Stat*) PTR_BAG( BODY_FUNC( CURR_FUNC ) );
/*      if (TLS->countNams > 0)
	TLS->countNams--;*/
    }
}


/****************************************************************************
**
*F  ReadWhile( <follow> ) . . . . . . . . . . . . . .  read a while statement
**
**  'ReadWhile' reads a while-loop.  In case of an error it skips all symbols
**  up to one contained in <follow>.
**
**  <Statement> := 'while' <Expr>  'do'
**                     <Statments>
**                 'od' ';'
*/
void ReadWhile (
    TypSymbolSet        follow )
{
    volatile UInt       nrs;            /* number of statements in body    */
    volatile UInt       nrError;        /* copy of <TLS->nrError>          */
    volatile Bag        currLVars;      /* copy of <TLS->currLVars>             */

    /* remember the current variables in case of an error                  */
    currLVars = TLS->currLVars;
    nrError   = TLS->nrError;

    /* 'while' <Expr>  'do'                                                */
    if ( ! READ_ERROR() ) { IntrWhileBegin(); }
    Match( S_WHILE, "while", follow );
    ReadExpr( S_DO|S_OD|follow, 'r' );
    Match( S_DO, "do", STATBEGIN|S_DO|follow );

    /*     <Statments>                                                     */
    if ( ! READ_ERROR() ) { IntrWhileBeginBody(); }
    nrs = ReadStats( S_OD|follow );
    if ( ! READ_ERROR() ) { IntrWhileEndBody( nrs ); }

    /* 'od'                                                                */
    Match( S_OD, "od", follow );
    if ( ! READ_ERROR() ) {
        IntrWhileEnd();
    }

    /* an error has occured *after* the 'IntrWhileEndBody'                 */
    /* If we hadn't actually come out of coding the body, we need
       to recover. Otherwise it was probably an error in executing the body and
       we just return */
    else if ( nrError == 0 && TLS->intrCoding ) {
        CodeEnd(1);
        TLS->intrCoding--;
        TLS->currLVars = currLVars;
        TLS->ptrLVars  = PTR_BAG( TLS->currLVars );
        TLS->ptrBody   = (Stat*) PTR_BAG( BODY_FUNC( CURR_FUNC ) );
    }
}

/****************************************************************************
**
*F  ReadAtomic( <follow> ) . . . . . . . . . . . . . .  read an atomic block
**
**  'ReadAtomic' reads an atomic block.  In case of an error it skips all symbols
**  up to one contained in <follow>.
**
**  <Statement> := 'atomic' <QualifiedExpression> { ',' <QualifiedExpression } 'do' <Statements> 'od' ';'
**
*/
void ReadAtomic (
    TypSymbolSet        follow )
{
    volatile UInt       nrs;            /* number of statements in body    */
    volatile UInt       nexprs;            /* number of statements in body    */
    volatile UInt       nrError;        /* copy of <TLS->nrError>          */
    volatile Bag        currLVars;      /* copy of <TLS->currLVars>         */
    volatile int        lockSP;         /* lock stack */

    /* remember the current variables in case of an error                  */
    currLVars = TLS->currLVars;
    nrError   = TLS->nrError;
    lockSP    = RegionLockSP();

    /* 'atomic' <QualifiedExpression> {',' <QualifiedExpression> } 'do'                                                */    
    if ( ! READ_ERROR() ) { IntrAtomicBegin(); }
    Match( S_ATOMIC, "atomic", follow );
    ReadQualifiedExpr( S_DO|S_OD|follow, 'r' );
    nexprs = 1;
    while (TLS->symbol == S_COMMA) {
      Match( S_COMMA, "comma", follow | S_DO | S_OD );
      ReadQualifiedExpr( S_DO|S_OD|follow, 'r' );
      nexprs ++;
      if (nexprs > MAX_ATOMIC_OBJS)
	{
	  SyntaxError("atomic statement can have at most 256 objects to lock");
	  return;
	}
    }

    Match( S_DO, "do or comma", STATBEGIN|S_DO|follow );

    /*     <Statments>                                                     */
    if ( ! READ_ERROR() ) { IntrAtomicBeginBody(nexprs); }
    nrs = ReadStats( S_OD|follow );
    if ( ! READ_ERROR() ) { IntrAtomicEndBody( nrs ); }

    /* 'od'                                                                */
    Match( S_OD, "od", follow );
    if ( ! READ_ERROR() ) {
        IntrAtomicEnd();
    }

    /* an error has occured *after* the 'IntrAtomicEndBody'                 */
    /* If we hadn't actually come out of coding the body, we need
       to recover. Otherwise it was probably an error in executing the body and
       we just return */
    else if ( nrError == 0 && TLS->intrCoding ) {
        CodeEnd(1);
        TLS->intrCoding--;
        TLS->currLVars = currLVars;
        TLS->ptrLVars  = PTR_BAG( TLS->currLVars );
        TLS->ptrBody   = (Stat*) PTR_BAG( BODY_FUNC( CURR_FUNC ) );
    }
    /* This is a no-op if IntrAtomicEnd() succeeded, otherwise it restores
     * locks to where they were before. */
    PopRegionLocks(lockSP);
}


/****************************************************************************
**
*F  ReadRepeat( <follow> )  . . . . . . . . . . . . . read a repeat statement
**
**  'ReadRepeat' reads a  repeat-loop.   In case  of an  error it skips   all
**  symbols up to one contained in <follow>.
**
** <Statement> := 'repeat'
**                    <Statments>
**                'until' <Expr> ';'
*/
void ReadRepeat (
    TypSymbolSet        follow )
{
    volatile UInt       nrs;            /* number of statements in body    */
    volatile UInt       nrError;        /* copy of <TLS->nrError>          */
    volatile Bag        currLVars;      /* copy of <TLS->currLVars>             */

    /* remember the current variables in case of an error                  */
    currLVars = TLS->currLVars;
    nrError   = TLS->nrError;

    /* 'repeat'                                                            */
    if ( ! READ_ERROR() ) { IntrRepeatBegin(); }
    Match( S_REPEAT, "repeat", follow );

    /*  <Statments>                                                        */
    if ( ! READ_ERROR() ) { IntrRepeatBeginBody(); }
    nrs = ReadStats( S_UNTIL|follow );
    if ( ! READ_ERROR() ) { IntrRepeatEndBody( nrs ); }

    /* 'until' <Expr>                                                      */
    Match( S_UNTIL, "until", EXPRBEGIN|follow );
    ReadExpr( follow, 'r' );
    if ( ! READ_ERROR() ) {
        IntrRepeatEnd();
    }

    /* an error has occured *after* the 'IntrFuncExprEnd'                  */
    /* If we hadn't actually come out of coding the body, we need
       to recover. Otherwise it was probably an error in executing the body and
       we just return */
    else if ( nrError == 0 && TLS->intrCoding ) {
        CodeEnd(1);
        TLS->intrCoding--;
        TLS->currLVars = currLVars;
        TLS->ptrLVars  = PTR_BAG( TLS->currLVars );
        TLS->ptrBody   = (Stat*) PTR_BAG( BODY_FUNC( CURR_FUNC ) );
    }
}


/****************************************************************************
**
*F  ReadBreak(<follow>) . . . . . . . . . . . . . . .  read a break statement
**
**  'ReadBreak' reads a  break-statement.  In case  of an error  it skips all
**  symbols up to one contained in <follow>.
**
**  <Statement> := 'break' ';'
*/
void ReadBreak (
    TypSymbolSet        follow )
{
    /* skip the break symbol                                               */
    Match( S_BREAK, "break", follow );

    /* interpret the break statement                                       */
    if ( ! READ_ERROR() ) { IntrBreak(); }
}

/****************************************************************************
**
*F  ReadContinue(<follow>) . . . . . . . . . . . . . . .  read a continue statement
**
**  'ReadContinue' reads a  continue-statement.  In case  of an error  it skips all
**  symbols up to one contained in <follow>.
**
**  <Statement> := 'continue' ';'
*/
void ReadContinue (
    TypSymbolSet        follow )
{
    /* skip the continue symbol                                               */
    Match( S_CONTINUE, "continue", follow );

    /* interpret the continue statement                                       */
    if ( ! READ_ERROR() ) { IntrContinue(); }
}


/****************************************************************************
**
*F  ReadReturn( <follow> )  . . . . . . . . . . . . . read a return statement
**
**  'ReadReturn'   reads  a  return-statement.   Return  with   no expression
**  following is used  in functions to return void.   In case of an error  it
**  skips all symbols up to one contained in <follow>.
**
**  <Statement> := 'return' [ <Expr> ] ';'
**
**  It is still legal to use parenthesis but they  are  no  longer  required,
**  a return statememt is not a function call and should not look  like  one.
*/
void ReadReturn (
    TypSymbolSet        follow )
{
    /* skip the return symbol                                              */
    Match( S_RETURN, "return", follow );

    /* 'return' with no expression following                               */
    if ( TLS->symbol == S_SEMICOLON ) {
        if ( ! READ_ERROR() ) { IntrReturnVoid(); }
    }

    /* 'return' with an expression following                               */
    else {
        ReadExpr( follow, 'r' );
        if ( ! READ_ERROR() ) { IntrReturnObj(); }
    }
}


/****************************************************************************
**
*F  ReadTryNext(<follow>) . . . . . . . . .  read a try-next-method statement
**
**  'ReadTryNext' reads a try-next-method statement.  In case of an error  it
**  skips all symbols up to one contained in <follow>.
**
**  <Statment> := 'TryNextMethod' '(' ')' ';'
*/
void ReadTryNext (
    TypSymbolSet        follow )
{
    Match( S_TRYNEXT, "TryNextMethod", follow );
    Match( S_LPAREN, "(", follow );
    Match( S_RPAREN, ")", follow );
    if ( ! READ_ERROR() ) {
        IntrRefGVar( GVarName( "TRY_NEXT_METHOD" ) );
        IntrReturnObj();
    }
}


/****************************************************************************
**
*F  ReadQuit( <follow> )  . . . . . . . . . . . . . . . read a quit statement
**
**  'ReadQuit' reads a  quit  statement.  In case   of an error it skips  all
**  symbols up to one contained in <follow>.
**
**  <Statement> := 'quit' ';'
*/
void            ReadQuit (
    TypSymbolSet        follow )
{
    /* skip the quit symbol                                                */
    Match( S_QUIT, "quit", follow );

    /* interpret the quit                                                  */
    if ( ! READ_ERROR() ) { IntrQuit(); }
}

/****************************************************************************
**
*F  ReadQUIT( <follow> )  . . . . . . . . . . . . . . . read a QUIT statement
**
**  'ReadQUIT' reads a  QUIT  statement.  In case   of an error it skips  all
**  symbols up to one contained in <follow>.
**
**  <Statement> := 'QUIT' ';'
*/
void            ReadQUIT (
    TypSymbolSet        follow )
{
    /* skip the quit symbol                                                */
    Match( S_QQUIT, "QUIT", follow );

    /* interpret the quit                                                  */
    if ( ! READ_ERROR() ) { IntrQUIT(); }
}


/****************************************************************************
**
*F  ReadStats(<follow>) . . . . . . . . . . . . . . read a statement sequence
**
**  'ReadStats' reads a statement sequence.  In case of an error it skips all
**  symbols up to one contained in <follow>.
**
**  <Statments> := { <Statment> }
**
**  <Statment>  := <Var> ':=' <Expr> ';'
**              |  <Var> '(' [ <Expr> { ',' <Expr> } ] ')' ';'
**              |  'Unbind' '(' <Var> ')' ';'
**              |  'if'   <Expr>  'then' <Statments>
**                 { 'elif' <Expr>  'then' <Statments> }
**                 [ 'else'                <Statments> ] 'fi' ';'
**              |  'for' <Var> 'in' <Expr> 'do' <Statments> 'od' ';'
**              |  'while' <Expr>  'do' <Statments>  'od' ';'
**              |  'repeat' <Statments>  'until' <Expr> ';'
**              |  'break' ';'
**              |  'return' [ <Expr> ] ';'
**              |  'atomic' <QualifiedExpression> { ',' <QualifiedExpression } 'do' <Statements> 'od' ';'
**              |  ';'
*/
UInt ReadStats (
    TypSymbolSet        follow )
{
    UInt               nr;            /* number of statements            */

    /* read the statements                                                 */
    nr = 0;
    while ( IS_IN( TLS->symbol, STATBEGIN|S_SEMICOLON ) ) {

        /* read a statement                                                */
        if      ( TLS->symbol == S_IDENT  ) ReadCallVarAss(follow,'s');
        else if ( TLS->symbol == S_UNBIND ) ReadUnbind(    follow    );
        else if ( TLS->symbol == S_INFO   ) ReadInfo(      follow    );
        else if ( TLS->symbol == S_ASSERT ) ReadAssert(    follow    );
        else if ( TLS->symbol == S_IF     ) ReadIf(        follow    );
        else if ( TLS->symbol == S_FOR    ) ReadFor(       follow    );
        else if ( TLS->symbol == S_WHILE  ) ReadWhile(     follow    );
        else if ( TLS->symbol == S_REPEAT ) ReadRepeat(    follow    );
        else if ( TLS->symbol == S_BREAK  ) ReadBreak(     follow    );
        else if ( TLS->symbol == S_CONTINUE) ReadContinue(     follow    );
        else if ( TLS->symbol == S_RETURN ) ReadReturn(    follow    );
        else if ( TLS->symbol == S_TRYNEXT) ReadTryNext(   follow    );
	else if ( TLS->symbol == S_QUIT   ) ReadQuit(      follow    );
	else if ( TLS->symbol == S_ATOMIC ) ReadAtomic(    follow    );
	else                           ReadEmpty(     follow    );
	nr++;
        Match( S_SEMICOLON, ";", follow );

    }

    /* return the number of statements                                     */
    return nr;
}


/****************************************************************************
**

*F * * * * * * * * * * * * read and evaluate symbols  * * * * * * * * * * * *
*/


/****************************************************************************
**

*V  ReadEvalResult  . . . . . . . . result of reading one command immediately
*/
/* TL: Obj ReadEvalResult; */


/****************************************************************************
**
*F  ReadEvalCommand() . . . . . . . . . . . . . . . . . . .  read one command
**
**  'ReadEvalCommand' reads one command and interprets it immediately.
**
**  It does not expect the  first symbol of its input  already read and  wont
**  read the  first symbol of the  next  input.
**
*/


void RecreateStackNams( Obj context )
{
  Obj lvars = context;
  Obj nams;
  UInt i;
  while (lvars != TLS->bottomLVars && lvars != (Obj)0)
    {
      nams = NAMS_FUNC(PTR_BAG(lvars)[0]);
      if (nams != (Obj) 0)
	{
	  GROW_PLIST(TLS->stackNams, ++TLS->countNams);
	  SET_ELM_PLIST( TLS->stackNams, TLS->countNams, nams);
	  SET_LEN_PLIST( TLS->stackNams, TLS->countNams);
	}
      lvars = ENVI_FUNC(PTR_BAG(lvars)[0]);
    }

  /* At this point we have the stack upside down, so invert it */
  for (i = 1; i <= TLS->countNams/2; i++)
    {
      nams = ELM_PLIST(TLS->stackNams, i);
      SET_ELM_PLIST( TLS->stackNams,
		     i,
		     ELM_PLIST(TLS->stackNams, TLS->countNams + 1 -i));
      SET_ELM_PLIST( TLS->stackNams,
		     TLS->countNams + 1 -i,
		     nams);
    }
}


ExecStatus ReadEvalCommand ( Obj context )
{
    ExecStatus          type;
    Obj                 stackNams;
    UInt                countNams;
    UInt                readTop;
    UInt                readTilde;
    UInt                currLHSGVar;
    Obj                 errorLVars;
    syJmp_buf           readJmpError;
    int			lockSP;

    /* get the first symbol from the input                                 */
    Match( TLS->symbol, "", 0UL );

    /* if we have hit <end-of-file>, then give up                          */
    if ( TLS->symbol == S_EOF )  { return STATUS_EOF; }

    /* print only a partial prompt from now on                             */
    if ( !SyQuiet )
      TLS->prompt = "> ";
    else
      TLS->prompt = "";

    /* remember the old reader context                                     */
    stackNams   = TLS->stackNams;
    countNams   = TLS->countNams;
    readTop     = TLS->readTop;
    readTilde   = TLS->readTilde;
    currLHSGVar = TLS->currLHSGVar;
    memcpy( readJmpError, TLS->readJmpError, sizeof(syJmp_buf) );

    /* intialize everything and begin an interpreter                       */
    TLS->stackNams   = NEW_PLIST( T_PLIST, 16 );
    TLS->countNams   = 0;
    TLS->readTop     = 0;
    TLS->readTilde   = 0;
    TLS->currLHSGVar = 0;
    RecreateStackNams(context);
    errorLVars = TLS->errorLVars;
    TLS->errorLVars = context;
    lockSP = RegionLockSP();

    IntrBegin( context );

    /* read an expression or an assignment or a procedure call             */
    if      ( TLS->symbol == S_IDENT  ) { ReadExpr(   S_SEMICOLON|S_EOF, 'x' ); }

    /* otherwise read a statement                                          */
    else if (TLS->symbol==S_UNBIND    ) { ReadUnbind( S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_INFO      ) { ReadInfo(   S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_ASSERT    ) { ReadAssert( S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_IF        ) { ReadIf(     S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_FOR       ) { ReadFor(    S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_WHILE     ) { ReadWhile(  S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_REPEAT    ) { ReadRepeat( S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_BREAK     ) { ReadBreak(  S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_CONTINUE     ) { ReadContinue(  S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_RETURN    ) { ReadReturn( S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_TRYNEXT   ) { ReadTryNext(S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_QUIT      ) { ReadQuit(   S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_QQUIT     ) { ReadQUIT(   S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_SEMICOLON ) { ReadEmpty(  S_SEMICOLON|S_EOF      ); }
    else if (TLS->symbol==S_ATOMIC )    { ReadAtomic(  S_SEMICOLON|S_EOF      ); }

    /* otherwise try to read an expression                                 */
    /* Unless the statement is empty, in which case do nothing             */
    else                           { ReadExpr(   S_SEMICOLON|S_EOF, 'r' ); }

    /* every statement must be terminated by a semicolon  \                 */
    if ( TLS->symbol != S_SEMICOLON ) {
        SyntaxError( "; expected");
    }

    /* check for dual semicolon                                            */
    if ( *TLS->in == ';' ) {
        GetSymbol();
        TLS->dualSemicolon = 1;
    }
    else {
        TLS->dualSemicolon = 0;
    }

    /* end the interpreter                                                 */
    if ( ! READ_ERROR() ) {
        type = IntrEnd( 0UL );
        PopRegionAutoLocks(lockSP);
    }
    else {
        IntrEnd( 1UL );
        type = STATUS_ERROR;
        PopRegionLocks(lockSP);
	if (TLS->CurrentHashLock)
	  HashUnlock(TLS->CurrentHashLock);
    }

    /* switch back to the old reader context                               */
    memcpy( TLS->readJmpError, readJmpError, sizeof(syJmp_buf) );
    TLS->stackNams   = stackNams;
    TLS->countNams   = countNams;
    TLS->readTop     = readTop;
    TLS->readTilde   = readTilde;
    TLS->currLHSGVar = currLHSGVar;
    TLS->errorLVars = errorLVars;

    /* copy the result (if any)                                            */
    TLS->readEvalResult = TLS->intrResult;

    /* return whether a return-statement or a quit-statement were executed */
    return type;
}

/****************************************************************************
**
*F  ReadEvalFile()  . . . . . . . . . . . . . . . . . . . . . . . read a file
**
**  'ReadEvalFile' reads an entire file and returns (in 'ReadEvalResult') the
**  entire file as thunk, i.e., as function of no argument.
**
**  It does not expect the  first symbol of its input  already read and  wont
**  reads to the end of the input (unless an error happens).
*/
UInt ReadEvalFile ( void )
{
    volatile ExecStatus type;
    volatile Obj        stackNams;
    volatile UInt       countNams;
    volatile UInt       readTop;
    volatile UInt       readTilde;
    volatile UInt       currLHSGVar;
    syJmp_buf             readJmpError;
    volatile UInt       nr;
    volatile Obj        name;
    volatile Obj        nams;
    volatile Int        nloc;
    volatile Int        i;
    volatile int	lockSP;

    /* get the first symbol from the input                                 */
    Match( TLS->symbol, "", 0UL );

    /* if we have hit <end-of-file>, then give up                          */
    if ( TLS->symbol == S_EOF )  { return STATUS_EOF; }

    /* print only a partial prompt from now on                             */
    if ( !SyQuiet )
      TLS->prompt = "> ";
    else
      TLS->prompt = "";

    /* remember the old reader context                                     */
    stackNams   = TLS->stackNams;
    countNams   = TLS->countNams;
    readTop     = TLS->readTop;
    readTilde   = TLS->readTilde;
    currLHSGVar = TLS->currLHSGVar;
    lockSP      = RegionLockSP();
    memcpy( readJmpError, TLS->readJmpError, sizeof(syJmp_buf) );

    /* intialize everything and begin an interpreter                       */
    TLS->stackNams   = NEW_PLIST( T_PLIST, 16 );
    TLS->countNams   = 0;
    TLS->readTop     = 0;
    TLS->readTilde   = 0;
    TLS->currLHSGVar = 0;
    IntrBegin(TLS->bottomLVars);

    /* check for local variables                                           */
    nloc = 0;
    nams = NEW_PLIST( T_PLIST, nloc );
    SET_LEN_PLIST( nams, nloc );
    TLS->countNams += 1;
    ASS_LIST( TLS->stackNams, TLS->countNams, nams );
    if ( TLS->symbol == S_LOCAL ) {
        Match( S_LOCAL, "local", 0L );
        name = NEW_STRING( strlen(TLS->value) );
        SyStrncat( CSTR_STRING(name), TLS->value, strlen(TLS->value) );
        nloc += 1;
        ASS_LIST( nams, nloc, name );
        Match( S_IDENT, "identifier", STATBEGIN|S_END );
        while ( TLS->symbol == S_COMMA ) {
            TLS->value[0] = '\0';
            Match( S_COMMA, ",", 0L );
            for ( i = 1; i <= nloc; i++ ) {
                if ( strcmp(CSTR_STRING(ELM_LIST(nams,i)),TLS->value) == 0 ) {
                    SyntaxError("name used for two locals");
                }
            }
            name = NEW_STRING( strlen(TLS->value) );
            SyStrncat( CSTR_STRING(name), TLS->value, strlen(TLS->value) );
            nloc += 1;
            ASS_LIST( nams, nloc, name );
            Match( S_IDENT, "identifier", STATBEGIN|S_END );
        }
        Match( S_SEMICOLON, ";", STATBEGIN|S_END );
    }

    /* fake the 'function ()'                                              */
    IntrFuncExprBegin( 0L, nloc, nams, TLS->input->number );

    /* read the statements                                                 */
    nr = ReadStats( S_SEMICOLON | S_EOF );

    /* we now want to be at <end-of-file>                                  */
    if ( TLS->symbol != S_EOF ) {
        SyntaxError("<end-of-file> expected");
    }

    /* fake the 'end;'                                                     */
    if ( ! READ_ERROR() ) {
        IntrFuncExprEnd( nr, 0UL );
    }
    else {
        Obj fexp;
        CodeEnd(1);
        TLS->intrCoding--;
        fexp = CURR_FUNC;
        if (fexp && ENVI_FUNC(fexp))  SWITCH_TO_OLD_LVARS(ENVI_FUNC(fexp));
    }

    /* end the interpreter                                                 */
    if ( ! READ_ERROR() ) {
        type = IntrEnd( 0UL );
    }
    else {
        IntrEnd( 1UL );
        type = STATUS_ERROR;
    }

    /* switch back to the old reader context                               */
    memcpy( TLS->readJmpError, readJmpError, sizeof(syJmp_buf) );
    PopRegionLocks(lockSP);
    if (TLS->CurrentHashLock)
      HashUnlock(TLS->CurrentHashLock);
    TLS->stackNams   = stackNams;
    TLS->countNams   = countNams;
    TLS->readTop     = readTop;
    TLS->readTilde   = readTilde;
    TLS->currLHSGVar = currLHSGVar;

    /* copy the result (if any)                                            */
    TLS->readEvalResult = TLS->intrResult;

    /* return whether a return-statement or a quit-statement were executed */
    return type;
}


/****************************************************************************
**
*F  ReadEvalError() . . . . . . . . . . . . . . . . . .  return with an error
*/
void            ReadEvalError ( void )
{
    TLS->ptrBody  = (Stat*)PTR_BAG(BODY_FUNC(CURR_FUNC));
    TLS->ptrLVars = PTR_BAG(TLS->currLVars);
    syLongjmp( TLS->readJmpError, 1 );
}


/****************************************************************************
**
*F  Call0ArgsInNewReader(Obj f)  . . . . . . . . . . . . call a GAP function
**
**  The current reader context is saved and a new one is started.
*/
Obj Call0ArgsInNewReader(Obj f)

{
  /* for the new interpreter context: */
/*  ExecStatus          type; */
  Obj                 stackNams;
  UInt                countNams;
  UInt                readTop;
  UInt                readTilde;
  UInt                currLHSGVar;
  UInt                userHasQuit;
  syJmp_buf             readJmpError;
  UInt                intrCoding;
  UInt                intrIgnoring;
  UInt                nrError;
  Obj result;

  /* remember the old reader context                                     */
  stackNams   = TLS->stackNams;
  countNams   = TLS->countNams;
  readTop     = TLS->readTop;
  readTilde   = TLS->readTilde;
  currLHSGVar = TLS->currLHSGVar;
  userHasQuit = TLS->UserHasQuit;
  intrCoding = TLS->intrCoding;
  intrIgnoring = TLS->intrIgnoring;
  nrError = TLS->nrError;
  memcpy( readJmpError, TLS->readJmpError, sizeof(syJmp_buf) );

  /* intialize everything and begin an interpreter                       */
  TLS->stackNams   = NEW_PLIST( T_PLIST, 16 );
  TLS->countNams   = 0;
  TLS->readTop     = 0;
  TLS->readTilde   = 0;
  TLS->currLHSGVar = 0;
  TLS->UserHasQuit = 0;
  TLS->intrCoding = 0;
  TLS->intrIgnoring = 0;
  TLS->nrError = 0;
  IntrBegin( TLS->bottomLVars );

  if (!READ_ERROR()) {
    result = CALL_0ARGS(f);
    PushVoidObj();
    /* end the interpreter                                                 */
    IntrEnd( 0UL );
  } else {
    result = (Obj) 0L;
    IntrEnd( 1UL );
    ClearError();
  }

  /* switch back to the old reader context                               */
  memcpy( TLS->readJmpError, readJmpError, sizeof(syJmp_buf) );
  TLS->UserHasQuit = userHasQuit;
  TLS->stackNams   = stackNams;
  TLS->countNams   = countNams;
  TLS->readTop     = readTop;
  TLS->readTilde   = readTilde;
  TLS->currLHSGVar = currLHSGVar;
  TLS->intrCoding = intrCoding;
  TLS->intrIgnoring = intrIgnoring;
  TLS->nrError = nrError;
  return result;
}

/****************************************************************************
**
*F  Call1ArgsInNewReader(Obj f,Obj a) . . . . . . . . . . call a GAP function
**
**  The current reader context is saved and a new one is started.
*/
Obj Call1ArgsInNewReader(Obj f,Obj a)

{
  /* for the new interpreter context: */
/*ExecStatus          type; */
  Obj                 stackNams;
  UInt                countNams;
  UInt                readTop;
  UInt                readTilde;
  UInt                currLHSGVar;
  UInt                userHasQuit;
  UInt                intrCoding;
  UInt                intrIgnoring;
  syJmp_buf             readJmpError;
  Obj result;
  UInt                nrError;

  /* remember the old reader context                                     */
  stackNams   = TLS->stackNams;
  countNams   = TLS->countNams;
  readTop     = TLS->readTop;
  readTilde   = TLS->readTilde;
  currLHSGVar = TLS->currLHSGVar;
  userHasQuit = TLS->UserHasQuit;
  intrCoding = TLS->intrCoding;
  intrIgnoring = TLS->intrIgnoring;
  nrError = TLS->nrError;
  memcpy( readJmpError, TLS->readJmpError, sizeof(syJmp_buf) );

  /* intialize everything and begin an interpreter                       */
  TLS->stackNams   = NEW_PLIST( T_PLIST, 16 );
  TLS->countNams   = 0;
  TLS->readTop     = 0;
  TLS->readTilde   = 0;
  TLS->currLHSGVar = 0;
  TLS->UserHasQuit = 0;
  TLS->intrCoding = 0;
  TLS->intrIgnoring = 0;
  TLS->nrError = 0;
  IntrBegin( TLS->bottomLVars );

  if (!READ_ERROR()) {
    result = CALL_1ARGS(f,a);
    PushVoidObj();
    /* end the interpreter                                                 */
    IntrEnd( 0UL );
  } else {
    result = (Obj) 0L;
    IntrEnd( 1UL );
    ClearError();
  }

  /* switch back to the old reader context                               */
  memcpy( TLS->readJmpError, readJmpError, sizeof(syJmp_buf) );
  TLS->intrCoding = intrCoding;
  TLS->intrIgnoring = intrIgnoring;
  TLS->stackNams   = stackNams;
  TLS->countNams   = countNams;
  TLS->readTop     = readTop;
  TLS->readTilde   = readTilde;
  TLS->currLHSGVar = currLHSGVar;
  TLS->UserHasQuit = userHasQuit;
  TLS->nrError = nrError;
  return result;
}


/****************************************************************************
**

*F * * * * * * * * * * * * * initialize package * * * * * * * * * * * * * * *
*/

/****************************************************************************
**
*F  InitKernel( <module> )  . . . . . . . . initialise kernel data structures
*/
static Int InitKernel (
    StructInitInfo *    module )
{
  TLS->errorLVars = (UInt **)0;
  TLS->currentGlobalForLoopDepth = 0;
    /* TL: InitGlobalBag( &ReadEvalResult, "src/read.c:ReadEvalResult" ); */
    /* TL: InitGlobalBag( &StackNams,      "src/read.c:StackNams"      ); */
    InitCopyGVar( "GAPInfo", &GAPInfo);
    /* return success                                                      */
    return 0;
}


/****************************************************************************
**
*F  InitInfoRead()  . . . . . . . . . . . . . . . . . table of init functions
*/
static StructInitInfo module = {
    MODULE_BUILTIN,                     /* type                           */
    "read",                             /* name                           */
    0,                                  /* revision entry of c file       */
    0,                                  /* revision entry of h file       */
    0,                                  /* version                        */
    0,                                  /* crc                            */
    InitKernel,                         /* initKernel                     */
    0,                                  /* initLibrary                    */
    0,                                  /* checkInit                      */
    0,                                  /* preSave                        */
    0,                                  /* postSave                       */
    0                                   /* postRestore                    */
};

StructInitInfo * InitInfoRead ( void )
{
    FillInVersion( &module );
    return &module;
}


/****************************************************************************
**

*E  read.c  . . . . . . . . . . . . . . . . . . . . . . . . . . . . ends here
*/



