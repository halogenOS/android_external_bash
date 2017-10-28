/* declare.c, created from declare.def. */
#line 22 "./declare.def"

#line 62 "./declare.def"

#line 70 "./declare.def"

#include <config.h>

#if defined (HAVE_UNISTD_H)
#  ifdef _MINIX
#    include <sys/types.h>
#  endif
#  include <unistd.h>
#endif

#include <stdio.h>

#include "../bashansi.h"
#include "../bashintl.h"

#include "../shell.h"
#include "common.h"
#include "builtext.h"
#include "bashgetopt.h"

extern int array_needs_making;
extern int posixly_correct;

static int declare_internal __P((register WORD_LIST *, int));

/* Declare or change variable attributes. */
int
declare_builtin (list)
     register WORD_LIST *list;
{
  return (declare_internal (list, 0));
}

#line 118 "./declare.def"
int
local_builtin (list)
     register WORD_LIST *list;
{
  if (variable_context)
    return (declare_internal (list, 1));
  else
    {
      builtin_error (_("can only be used in a function"));
      return (EXECUTION_FAILURE);
    }
}

#if defined (ARRAY_VARS)
#  define DECLARE_OPTS	"+acfgilnprtuxAF"
#else
#  define DECLARE_OPTS	"+cfgilnprtuxF"
#endif

/* The workhorse function. */
static int
declare_internal (list, local_var)
     register WORD_LIST *list;
     int local_var;
{
  int flags_on, flags_off, *flags;
  int any_failed, assign_error, pflag, nodefs, opt, mkglobal, onref, offref;
  char *t, *subscript_start;
  SHELL_VAR *var, *refvar, *v;
  FUNCTION_DEF *shell_fn;

  flags_on = flags_off = any_failed = assign_error = pflag = nodefs = mkglobal = 0;
  refvar = (SHELL_VAR *)NULL;
  reset_internal_getopt ();
  while ((opt = internal_getopt (list, DECLARE_OPTS)) != EOF)
    {
      flags = list_opttype == '+' ? &flags_off : &flags_on;

      switch (opt)
	{
	case 'a':
#if defined (ARRAY_VARS)
	  *flags |= att_array;
	  break;
#else
	  builtin_usage ();
	  return (EX_USAGE);
#endif
	case 'A':
#if defined (ARRAY_VARS)
	  *flags |= att_assoc;
	  break;
#else
	  builtin_usage ();
	  return (EX_USAGE);
#endif
	case 'p':
	  if (local_var == 0)
	    pflag++;
	  break;
        case 'F':
	  nodefs++;
	  *flags |= att_function;
	  break;
	case 'f':
	  *flags |= att_function;
	  break;
	case 'g':
	  if (flags == &flags_on)
	    mkglobal = 1;
	  break;
	case 'i':
	  *flags |= att_integer;
	  break;
	case 'n':
	  *flags |= att_nameref;
	  break;
	case 'r':
	  *flags |= att_readonly;
	  break;
	case 't':
	  *flags |= att_trace;
	  break;
	case 'x':
	  *flags |= att_exported;
	  array_needs_making = 1;
	  break;
#if defined (CASEMOD_ATTRS)
#  if defined (CASEMOD_CAPCASE)
	 case 'c':
	  *flags |= att_capcase;
	  if (flags == &flags_on)
	    flags_off |= att_uppercase|att_lowercase;
	  break;
#  endif
	case 'l':
	  *flags |= att_lowercase;
	  if (flags == &flags_on)
	    flags_off |= att_capcase|att_uppercase;
	  break;
	case 'u':
	  *flags |= att_uppercase;
	  if (flags == &flags_on)
	    flags_off |= att_capcase|att_lowercase;
	  break;
#endif /* CASEMOD_ATTRS */
	default:
	  builtin_usage ();
	  return (EX_USAGE);
	}
    }

  list = loptend;

  /* If there are no more arguments left, then we just want to show
     some variables. */
  if (list == 0)	/* declare -[aAfFirtx] */
    {
      /* Show local variables defined at this context level if this is
	 the `local' builtin. */
      if (local_var)
	{
	  register SHELL_VAR **vlist;
	  register int i;

	  vlist = all_local_variables ();

	  if (vlist)
	    {
	      for (i = 0; vlist[i]; i++)
		print_assignment (vlist[i]);

	      free (vlist);
	    }
	}
      else if (pflag && (flags_on == 0 || flags_on == att_function))
	show_all_var_attributes (flags_on == 0, nodefs);
      else if (flags_on == 0)
	return (set_builtin ((WORD_LIST *)NULL));
      else
	set_or_show_attributes ((WORD_LIST *)NULL, flags_on, nodefs);

      return (sh_chkwrite (EXECUTION_SUCCESS));
    }

  if (pflag)	/* declare -p [-aAfFirtx] name [name...] */
    {
      for (any_failed = 0; list; list = list->next)
	{
	  if (flags_on & att_function)
	    pflag = show_func_attributes (list->word->word, nodefs);
	  else
	    pflag = show_name_attributes (list->word->word, nodefs);
	  if (pflag)
	    {
	      sh_notfound (list->word->word);
	      any_failed++;
	    }
	}
      return (sh_chkwrite (any_failed ? EXECUTION_FAILURE : EXECUTION_SUCCESS));
    }

#define NEXT_VARIABLE() free (name); list = list->next; continue

  /* There are arguments left, so we are making variables. */
  while (list)		/* declare [-aAfFirx] name [name ...] */
    {
      char *value, *name;
      int offset, aflags;
#if defined (ARRAY_VARS)
      int making_array_special, compound_array_assign, simple_array_assign;
#endif

      name = savestring (list->word->word);
      offset = assignment (name, 0);
      aflags = 0;

      if (offset)	/* declare [-aAfFirx] name=value */
	{
	  name[offset] = '\0';
	  value = name + offset + 1;
	  if (name[offset - 1] == '+')
	    {
	      aflags |= ASS_APPEND;
	      name[offset - 1] = '\0';
	    }
	}
      else
	value = "";

      /* Do some lexical error checking on the LHS and RHS of the assignment
	 that is specific to nameref variables. */
      if (flags_on & att_nameref)
	{
#if defined (ARRAY_VARIABLES)
	  if (valid_array_reference (name))
	    {
	      builtin_error (_("%s: reference variable cannot be an array"), name);
	      assign_error++;
	      NEXT_VARIABLE ();
	    }
	  else
#endif
	  /* disallow self references at global scope */
	  if (STREQ (name, value) && variable_context == 0)
	    {
	      builtin_error (_("%s: nameref variable self references not allowed"), name);
	      assign_error++;
	      NEXT_VARIABLE ();
	    }
	}

#if defined (ARRAY_VARS)
      compound_array_assign = simple_array_assign = 0;
      subscript_start = (char *)NULL;
      if (t = strchr (name, '['))	/* ] */
	{
	  /* If offset != 0 we have already validated any array reference */
	  if (offset == 0 && valid_array_reference (name, 0) == 0)
	    {
	      sh_invalidid (name);
	      assign_error++;
	      NEXT_VARIABLE ();
	    }
	  subscript_start = t;
	  *t = '\0';
	  making_array_special = 1;
	}
      else
	making_array_special = 0;
#endif

      /* If we're in posix mode or not looking for a shell function (since
	 shell function names don't have to be valid identifiers when the
	 shell's not in posix mode), check whether or not the argument is a
	 valid, well-formed shell identifier. */
      if ((posixly_correct || (flags_on & att_function) == 0) && legal_identifier (name) == 0)
	{
	  sh_invalidid (name);
	  assign_error++;
	  NEXT_VARIABLE ();
	}

      /* If VARIABLE_CONTEXT has a non-zero value, then we are executing
	 inside of a function.  This means we should make local variables,
	 not global ones. */

      /* XXX - this has consequences when we're making a local copy of a
	       variable that was in the temporary environment.  Watch out
	       for this. */
      refvar = (SHELL_VAR *)NULL;
      if (variable_context && mkglobal == 0 && ((flags_on & att_function) == 0))
	{
#if defined (ARRAY_VARS)
	  if (flags_on & att_assoc)
	    var = make_local_assoc_variable (name);
	  else if ((flags_on & att_array) || making_array_special)
	    var = make_local_array_variable (name, making_array_special);
	  else
#endif
	    var = make_local_variable (name);	/* sets att_invisible for new vars */
	  if (var == 0)
	    {
	      any_failed++;
	      NEXT_VARIABLE ();
	    }
	}
      else
	var = (SHELL_VAR *)NULL;

      /* If we are declaring a function, then complain about it in some way.
	 We don't let people make functions by saying `typeset -f foo=bar'. */

      /* There should be a way, however, to let people look at a particular
	 function definition by saying `typeset -f foo'. */

      if (flags_on & att_function)
	{
	  if (offset)	/* declare -f [-rix] foo=bar */
	    {
	      builtin_error (_("cannot use `-f' to make functions"));
	      free (name);
	      return (EXECUTION_FAILURE);
	    }
	  else		/* declare -f [-rx] name [name...] */
	    {
	      var = find_function (name);

	      if (var)
		{
		  if (readonly_p (var) && (flags_off & att_readonly))
		    {
		      builtin_error (_("%s: readonly function"), name);
		      any_failed++;
		      NEXT_VARIABLE ();
		    }

		  /* declare -[Ff] name [name...] */
		  if (flags_on == att_function && flags_off == 0)
		    {
#if defined (DEBUGGER)
		      if (nodefs && debugging_mode)
			{
			  shell_fn = find_function_def (var->name);
			  if (shell_fn)
			    printf ("%s %d %s\n", var->name, shell_fn->line, shell_fn->source_file);
			  else
			    printf ("%s\n", var->name);
			}
		      else
#endif /* DEBUGGER */
			{	
			  t = nodefs ? var->name
				     : named_function_string (name, function_cell (var), FUNC_MULTILINE|FUNC_EXTERNAL);
			  printf ("%s\n", t);
			  any_failed = sh_chkwrite (any_failed);
			}
		    }
		  else		/* declare -[fF] -[rx] name [name...] */
		    {
		      VSETATTR (var, flags_on);
		      VUNSETATTR (var, flags_off);
		    }
		}
	      else
		any_failed++;
	      NEXT_VARIABLE ();
	    }
	}
      else		/* declare -[aAirx] name [name...] */
	{
	  /* Non-null if we just created or fetched a local variable. */
	  /* Here's what ksh93 seems to do.  If we are modifying an existing
	     nameref variable, we don't follow the nameref chain past the last
	     nameref, and we set the nameref variable's value so future
	     references to that variable will return the value of the variable
	     we're assigning right now. */
	  if (var == 0 && (flags_on & att_nameref))
	    {
	      /* See if we are trying to modify an existing nameref variable */
	      var = mkglobal ? find_global_variable_last_nameref (name, 0) : find_variable_last_nameref (name);
	      if (var && nameref_p (var) == 0)
		var = 0;
	    }
	  /* However, if we're turning off the nameref attribute on an existing
	     nameref variable, we first follow the nameref chain to the end,
	     modify the value of the variable this nameref variable references,
	     *CHANGING ITS VALUE AS A SIDE EFFECT* then turn off the nameref
	     flag *LEAVING THE NAMEREF VARIABLE'S VALUE UNCHANGED* */
	  else if (var == 0 && (flags_off & att_nameref))
	    {
	      /* See if we are trying to modify an existing nameref variable */
	      refvar = mkglobal ? find_global_variable_last_nameref (name, 0) : find_variable_last_nameref (name);
	      if (refvar && nameref_p (refvar) == 0)
		refvar = 0;
	      if (refvar)
		var = mkglobal ? find_global_variable (nameref_cell (refvar)) : find_variable (nameref_cell (refvar));
	    }
	
	  if (var == 0)
	    var = mkglobal ? find_global_variable (name) : find_variable (name);

	  if (var == 0)
	    {
#if defined (ARRAY_VARS)
	      if (flags_on & att_assoc)
		{
		  var = make_new_assoc_variable (name);
		  if (offset == 0)
		    VSETATTR (var, att_invisible);
		}
	      else if ((flags_on & att_array) || making_array_special)
		{
		  var = make_new_array_variable (name);
		  if (offset == 0)
		    VSETATTR (var, att_invisible);
		}
	      else
#endif

	      if (offset)
		var = mkglobal ? bind_global_variable (name, "", 0) : bind_variable (name, "", 0);
	      else
		{
		  var = mkglobal ? bind_global_variable (name, (char *)NULL, 0) : bind_variable (name, (char *)NULL, 0);
		  VSETATTR (var, att_invisible);
		}
	    }
	  /* Can't take an existing array variable and make it a nameref */
	  else if ((array_p (var) || assoc_p (var)) && (flags_on & att_nameref))
	    {
	      builtin_error (_("%s: reference variable cannot be an array"), name);
	      assign_error++;
	      NEXT_VARIABLE ();
	    }
	  else if (flags_on & att_nameref)
	    {
	      /* ksh93 compat: turning on nameref attribute turns off -ilu */
	      VUNSETATTR (var, att_integer|att_uppercase|att_lowercase|att_capcase);
	    }

	  /* Cannot use declare +r to turn off readonly attribute. */ 
	  if (readonly_p (var) && (flags_off & att_readonly))
	    {
	      sh_readonly (name);
	      any_failed++;
	      NEXT_VARIABLE ();
	    }

	  /* Cannot use declare to assign value to readonly or noassign
	     variable. */
	  if ((readonly_p (var) || noassign_p (var)) && offset)
	    {
	      if (readonly_p (var))
		sh_readonly (name);
	      assign_error++;
	      NEXT_VARIABLE ();
	    }

#if defined (ARRAY_VARS)
	  if ((making_array_special || (flags_on & (att_array|att_assoc)) || array_p (var) || assoc_p (var)) && offset)
	    {
	      int vlen;
	      vlen = STRLEN (value);

	      if (value[0] == '(' && value[vlen-1] == ')')
		compound_array_assign = 1;
	      else
		simple_array_assign = 1;
	    }

	  /* Cannot use declare +a name or declare +A name to remove an
	     array variable. */
	  if (((flags_off & att_array) && array_p (var)) || ((flags_off & att_assoc) && assoc_p (var)))
	    {
	      builtin_error (_("%s: cannot destroy array variables in this way"), name);
	      any_failed++;
	      NEXT_VARIABLE ();
	    }

	  if ((flags_on & att_array) && assoc_p (var))
	    {
	      builtin_error (_("%s: cannot convert associative to indexed array"), name);
	      any_failed++;
	      NEXT_VARIABLE ();
	    }
	  if ((flags_on & att_assoc) && array_p (var))
	    {
	      builtin_error (_("%s: cannot convert indexed to associative array"), name);
	      any_failed++;
	      NEXT_VARIABLE ();
	    }

	  /* declare -A name[[n]] makes name an associative array variable. */
	  if (flags_on & att_assoc)
	    {
	      if (assoc_p (var) == 0)
		var = convert_var_to_assoc (var);
	    }
	  /* declare -a name[[n]] or declare name[n] makes name an indexed
	     array variable. */
	  else if ((making_array_special || (flags_on & att_array)) && array_p (var) == 0 && assoc_p (var) == 0)
	    var = convert_var_to_array (var);
#endif /* ARRAY_VARS */

	  /* XXX - we note that we are turning on nameref attribute and defer
	     setting it until the assignment has been made so we don't do an
	     inadvertent nameref lookup.  Might have to do the same thing for
	     flags_off&att_nameref. */
	  /* XXX - ksh93 makes it an error to set a readonly nameref variable
	     using a single typeset command. */
	  onref = (flags_on & att_nameref);
	  flags_on &= ~att_nameref;
#if defined (ARRAY_VARS)
	  if (array_p (var) || assoc_p (var)
		|| (offset && compound_array_assign)
		|| simple_array_assign)
	    onref = 0;		/* array variables may not be namerefs */
#endif

	  /* ksh93 seems to do this */
	  offref = (flags_off & att_nameref);
	  flags_off &= ~att_nameref;

	  VSETATTR (var, flags_on);
	  VUNSETATTR (var, flags_off);

#if defined (ARRAY_VARS)
	  if (offset && compound_array_assign)
	    assign_array_var_from_string (var, value, aflags);
	  else if (simple_array_assign && subscript_start)
	    {
	      /* declare [-aA] name[N]=value */
	      *subscript_start = '[';	/* ] */
	      var = assign_array_element (name, value, 0);	/* XXX - not aflags */
	      *subscript_start = '\0';
	      if (var == 0)	/* some kind of assignment error */
		{
		  assign_error++;
		  flags_on |= onref;
		  flags_off |= offref;
		  NEXT_VARIABLE ();
		}
	    }
	  else if (simple_array_assign)
	    {
	      /* let bind_{array,assoc}_variable take care of this. */
	      if (assoc_p (var))
		bind_assoc_variable (var, name, savestring ("0"), value, aflags);
	      else
		bind_array_variable (name, 0, value, aflags);
	    }
	  else
#endif
	  /* bind_variable_value duplicates the essential internals of
	     bind_variable() */
	  if (offset)
	    {
	      if (onref)
		aflags |= ASS_NAMEREF;
	      v = bind_variable_value (var, value, aflags);
	      if (v == 0 && onref)
		{
		  sh_invalidid (value);
		  assign_error++;
		  /* XXX - unset this variable? or leave it as normal var? */
		  delete_var (var->name, mkglobal ? global_variables : shell_variables);
		  NEXT_VARIABLE ();
		}
	    }

	  /* If we found this variable in the temporary environment, as with
	     `var=value declare -x var', make sure it is treated identically
	     to `var=value export var'.  Do the same for `declare -r' and
	     `readonly'.  Preserve the attributes, except for att_tempvar. */
	  /* XXX -- should this create a variable in the global scope, or
	     modify the local variable flags?  ksh93 has it modify the
	     global scope.
	     Need to handle case like in set_var_attribute where a temporary
	     variable is in the same table as the function local vars. */
	  if ((flags_on & (att_exported|att_readonly)) && tempvar_p (var))
	    {
	      SHELL_VAR *tv;
	      char *tvalue;

	      tv = find_tempenv_variable (var->name);
	      if (tv)
		{
		  tvalue = var_isset (var) ? savestring (value_cell (var)) : savestring ("");
	          tv = bind_variable (var->name, tvalue, 0);
	          tv->attributes |= var->attributes & ~att_tempvar;
	          if (tv->context > 0)
		    VSETATTR (tv, att_propagate);
	          free (tvalue);
		}
	      VSETATTR (var, att_propagate);
	    }
	}

      /* Turn on nameref attribute we deferred above. */
      /* XXX - should we turn on the noassign attribute for consistency with
	 ksh93 when we turn on the nameref attribute? */
      VSETATTR (var, onref);
      flags_on |= onref;
      VUNSETATTR (var, offref);
      flags_off |= offref;
      /* Yuck.  ksh93 compatibility */
      if (refvar)
 	VUNSETATTR (refvar, flags_off);

      stupidly_hack_special_variables (name);

      NEXT_VARIABLE ();
    }

  return (assign_error ? EX_BADASSIGN
		       : ((any_failed == 0) ? EXECUTION_SUCCESS
  					    : EXECUTION_FAILURE));
}
