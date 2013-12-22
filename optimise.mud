/*
 * Copyright (c) 1993-2012 David Gay
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose, without fee, and without written agreement is hereby granted,
 * provided that the above copyright notice and the following two paragraphs
 * appear in all copies of this software.
 *
 * IN NO EVENT SHALL DAVID GAY BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT,
 * SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF
 * THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF DAVID GAY HAVE BEEN ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * DAVID GAY SPECIFICALLY DISCLAIM ANY WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND DAVID
 * GAY HAVE NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES,
 * ENHANCEMENTS, OR MODIFICATIONS.
 */

library optimise // The actual optimisations
requires compiler, vars, flow, ins3, phase2, graph, dlist, sequences, misc
defines mc:optimise_functions, mc:recompute_vars, mc:fold_branch
reads mc:verbose
writes mc:lineno, mc:this_function
[
  | fold, compute_ops, branch_ops, useless_instructions, remove_instruction,
    fold_constants, propagate_copies, propagate_closure_constants,
    propagate_closure_constant, eliminate_dead_code, change,
    pfoldbranch, partialfold, replace_use, remaining_fns, optimise_function,
    compute_trap_types, check_compute_trap, convert_to_type_trap,
    consttype, simple_equal? |

  fold = fn (ops, op, args, dofold)
    // Types: ops: array of function
    //        op : integer (index valid for ops)
    //        args : list of var
    //        dofold : fn (any)
    // Effects: If all args are constants, operation can be folded.
    //   ops[op](args) is called to do the actual folding, and the
    //   result is passed to dofold.
    // Returns: true if the operation was folded
    if (lforall?(fn (v) v[mc:v_class] == mc:v_constant, args) &&
	op < vector_length(ops))
      [
	| val, doop |

	doop = ops[op];
	if (args == null)
	  val = doop()
	else if (cdr(args) == null)
	  val = doop(car(args)[mc:v_kvalue])
	else if (cddr(args) == null)
	  val = doop(car(args)[mc:v_kvalue], cadr(args)[mc:v_kvalue])
	else
	  fail();
	if (val) [ dofold(cdr(val)); true ]
	else false
      ]
    else
      false;

  compute_ops = sequence
    (false,                     // sc_or
     false,                     // sc_and
     fn (x, y) true . x == y,
     fn (x, y) true . x != y,
     fn (x, y) if (integer?(x) && integer?(y)) true . x < y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x <= y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x > y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x >= y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x | y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x ^ y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x & y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x << y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x >> y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x + y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x - y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x * y else false,
     fn (x, y) if (!integer?(x) || !integer?(y) || y == 0) false else true . x / y,
     fn (x, y) if (!integer?(x) || !integer?(y) || y == 0) false else true . x % y,
     fn (x) if (integer?(x)) true . -x else false,
     fn (x) true . !x,
     fn (x) if (integer?(x)) true . ~x else false,
     false,                     // ifelse
     false,                     // if
     false,                     // while
     false,                     // loop
     fn (x, y)
       if (integer?(y))
         if (vector?(x))
           if (y >= -vector_length(x) && y < vector_length(x)) true . x[y]
	   else false
         else if (string?(x))
           if (y >= -string_length(x) && y < string_length(x)) true . x[y]
	   else false
         else false
       else if (string?(y) && table?(x)) true . x[y]
       else false,
     false,                     // set
     fn (x, y) false,           // cons's are mutable
     fn (x) false,              // assign
     fn (x) if (pair?(x)) true . car(x) else false,
     fn (x) if (pair?(x)) true . cdr(x) else false,
     fn (x) if (string?(x)) true . string_length(x) else false,
     fn (x) if (vector?(x)) true . vector_length(x) else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x + y else false,
     fn (x) true . typeof(x),
     fn () false,               // loop_count
     fn () false,               // max_loop_count
     fn (x) if (symbol?(x)) true . symbol_name(x) else false,
     fn (x) if (symbol?(x)) true . symbol_get(x) else false);
  assert(vlength(compute_ops) == mc:builtins);

  branch_ops = sequence
    (fn () false,
     fn () false,
     fn (x) true . x,
     fn (x) true . !x,
     fn (x, y) true . (x || y),
     fn (x, y) true . !(x || y),
     fn (x, y) true . (x && y),
     fn (x, y) true . !(x && y),
     fn (x, y) true . x == y,
     fn (x, y) true . x != y,
     fn (x, y) if (integer?(x) && integer?(y)) true . x < y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x >= y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x <= y else false,
     fn (x, y) if (integer?(x) && integer?(y)) true . x > y else false,
     fn (x, y) true . equal?(x, y),
     fn (x, y) true . !equal?(x, y),
     fn (x) true . immutable?(x),
     fn (x) true . !immutable?(x),
     fn (x) true . readonly?(x),
     fn (x) true . !readonly?(x));
  assert(vlength(branch_ops) == mc:branch_type?);

  // false means cannot trap
  // stype_any means always perform operation
  // type_xxx means both arguments have to be of this type
  compute_trap_types = sequence
    (false,        // sc_or
     false,        // sc_and
     false,        // eq
     false,        // ne
     type_integer, // lt
     type_integer, // le
     type_integer, // gt
     type_integer, // ge
     type_integer, // bitor
     type_integer, // bitxor
     type_integer, // bitand
     type_integer, // shift_left
     type_integer, // shift_right
     null,         // add -- special case below
     type_integer, // subtract
     type_integer, // multiply
     stype_any,    // divide -- may div zero trap
     stype_any,    // remainder -- may div zero trap
     type_integer, // negate
     false,        // not
     type_integer, // bitnot
     false,        // ifelse
     false,        // if
     false,        // while
     false,        // loop
     stype_any,    // ref
     false,        // set
     false,        // cons
     false,        // assign
     type_pair,    // car
     type_pair,    // cdr
     type_string,  // slength
     type_vector,  // vlength
     type_integer, // iadd
     false,        // typeof
     false,        // loop_count
     false,        // max_loop_count
     type_symbol,  // symbol_name
     type_symbol); // symbol_get
  assert(vlength(compute_trap_types) == mc:builtins);

  mc:fold_branch = fn (il, val) // folding function for branches
    [
      | ins, prevline |

      ins = il[mc:il_ins];
      prevline = mc:lineno;
      mc:lineno = il[mc:il_lineno];
      mc:warning("branch is %s taken", if (val) "always" else "never");
      mc:lineno = prevline;

      // change branch & remove graph edge
      if (val)
	[
	  ins[mc:i_bop] = mc:branch_always;
	  // remove fall through edge
	  graph_edges_out_apply(fn (e)
				  if (graph_edge_get(e)) graph_remove_edge(e),
				il[mc:il_node]);
	]
      else
	[
	  | to |

	  ins[mc:i_bop] = mc:branch_never;
	  to = ins[mc:i_bdest][mc:l_ins][mc:il_node];
	  // remove label's edge
	  graph_edges_out_apply(fn (e)
				  if (graph_edge_to(e) == to) graph_remove_edge(e),
				il[mc:il_node]);
	];
      ins[mc:i_bargs] = null;
      change = true;
      if (mc:verbose >= 3)
	[
	  dformat("FOLD %s\n", il[mc:il_number]);
	];
    ];

  partialfold = fn (args, pfold)
    [
      | arg1, arg2, t |

      arg1 = car(args); arg2 = cadr(args);
      if (arg1[mc:v_class] == mc:v_constant)
	[
	  t = arg1; arg1 = arg2; arg2 = t;
	];
      if (arg2[mc:v_class] == mc:v_constant)
	pfold(arg1, arg2[mc:v_kvalue]);
    ];

  pfoldbranch = fn (il, bop, arg)
    [
      | ins |

      ins = il[mc:il_ins];
      ins[mc:i_bop] = bop;
      ins[mc:i_bargs] = arg . null;
      if (mc:verbose >= 3)
	[
	  dformat("FOLDOR %s\n", il[mc:il_number]);
	];
    ];

  // true if arg can use '==' instead of 'equal?()'
  simple_equal? = fn (arg)
    [
      | t |
      t = consttype(arg);
      (t >= 0
       && !((1 << t) & (1 << type_symbol | 1 << type_vector | 1 << type_pair
                        | 1 << type_table | 1 << type_string | 1 << type_float
                        | 1 << type_bigint)))
    ];

  fold_constants = fn (il)
    // Types: il : instruction
    // Effects: Does constant folding on given instruction
    [
      | ins, class |

      ins = il[mc:il_ins];
      class = ins[mc:i_class];

      if (class == mc:i_compute)
	[
	  | op, args, foldtocst |

	  op = ins[mc:i_aop]; args = ins[mc:i_aargs];
	  foldtocst = fn (val) // folding function
	    [
	      ins[mc:i_aop] = mc:b_assign;
	      ins[mc:i_aargs] = mc:var_make_constant(val) . null;
	      if (mc:verbose >= 3)
		[
		  dformat("FOLD %s\n", il[mc:il_number]);
		];
	      change = true;
	    ];
	  fold(compute_ops, op, args, foldtocst)
	]
      else if (class == mc:i_branch)
	[
	  | op, args |

	  op = ins[mc:i_bop]; args = ins[mc:i_bargs];
	  if (!fold(branch_ops, ins[mc:i_bop], ins[mc:i_bargs],
		    fn (val) mc:fold_branch(il, val)))
	    // special cases for partial constant folding
	    if (op == mc:branch_or || op == mc:branch_nor ||
		op == mc:branch_and || op == mc:branch_nand)
	      partialfold
		(args,
		 fn (arg1, x)
		 [
		   if (op == mc:branch_nand && !x ||
		       op == mc:branch_or && x)
		     mc:fold_branch(il, true)
		   else if (op == mc:branch_and && !x ||
			    op == mc:branch_nor && x)
		     mc:fold_branch(il, false)
		   else if (op == mc:branch_or || op == mc:branch_and)
		     pfoldbranch(il, mc:branch_true, arg1)
		   else
		     pfoldbranch(il, mc:branch_false, arg1)
		 ])
          else if ((op == mc:branch_equal || op == mc:branch_nequal) &&
                   (simple_equal?(car(args)) || simple_equal?(cadr(args))))
            // change into regular == or !=
            ins[mc:i_bop] += mc:branch_eq - mc:branch_equal;
	]
      else if (class == mc:i_call)
        [
          | fun, args |
          @(fun . args) = ins[mc:i_cargs];
          if (fun[mc:v_class] == mc:v_global_constant
              && function?(fun = global_value(fun[mc:v_kvalue]))
              && !closure?(fun)
              && (primitive_flags(fun) & OP_CONST)
              && lforall?(fn (v) v[mc:v_class] == mc:v_constant, args))
            [
              | result, got_error |
              args = list_to_vector(lmap(fn (v) v[mc:v_kvalue], args));
              if (callable?(fun, vlength(args)))
                result = trap_error(fn () apply(fun, args),
                                    fn (n) got_error = n,
                                    call_trace_off,
                                    true)
              else
                got_error = error_wrong_parameters;

              | prevline |
              prevline = mc:lineno;
              mc:lineno = il[mc:il_lineno];
              if (got_error != null)
                [
                  mc:warning("call to %s() causes %s error",
                             function_name(fun), error_messages[got_error]);
                  ins = vector(mc:i_trap, mc:trap_always, got_error,
                               null, false)
                ]
              else
                [
                  result = check_immutable(result);
                  if (mc:verbose >= 3)
                    mc:warning("fold const call %s() -> %w",
                               function_name(fun), result);
                  assert(immutable?(result) && readonly?(result));
                  ins = vector(mc:i_compute, mc:b_assign, ins[mc:i_cdest],
                               list(mc:var_make_constant(result)), false);
                ];
              mc:lineno = prevline;
              il[mc:il_ins] = ins;
              change = true
            ]
        ];
    ];

  replace_use = fn (use, dvar, repvar, closure_ok?, is_fn?)
    [
      | ins, class, rep, replist |

      rep = fn (v) if (v == dvar) repvar else v;
      replist = fn (s)
        while (s != null)
          [
            if (car(s) == dvar) set_car!(s, repvar);
            s = cdr(s);
          ];

      ins = use[mc:il_ins];
      if (ins == null) exit<function> 0; // instruction is gone

      class = ins[mc:i_class];
      if (is_fn?)
        if (class == mc:i_closure)
          // fall through to regular treatment below
          null
        else if (class == mc:i_call)
          [
            if (car(ins[mc:i_cargs]) == dvar)
              ins[mc:i_cfunction] = repvar;
            exit<function> null;
          ]
        else
          exit<function> null
      else
        [
          if (mc:verbose >= 3)
            [
              dformat("COPY %s to %s in %s\n",
                      mc:svar(dvar), mc:svar(repvar), use[mc:il_number]);
            ];
          change = true;
        ];

      if (class == mc:i_compute)
        replist(ins[mc:i_aargs])
      else if (class == mc:i_branch)
        replist(ins[mc:i_bargs])
      else if (class == mc:i_trap)
        replist(ins[mc:i_targs])
      else if (class == mc:i_memory)
        [
          ins[mc:i_marray] = rep(ins[mc:i_marray]);
          ins[mc:i_mindex] = rep(ins[mc:i_mindex]);
          if (ins[mc:i_mop] == mc:memory_write ||
              ins[mc:i_mop] == mc:memory_write_safe)
            ins[mc:i_mscalar] = rep(ins[mc:i_mscalar])
        ]
      else if (class == mc:i_call)
        [
          replist(ins[mc:i_cargs]);
          if (car(ins[mc:i_cargs]) == repvar)
            mc:inline_builtin_call(use);
        ]
      else if (class == mc:i_vref)
        ins[mc:i_varg] = rep(ins[mc:i_varg])
      else if (class == mc:i_return)
        ins[mc:i_rvalue] = rep(ins[mc:i_rvalue])
      else if (class == mc:i_closure && closure_ok?)
        [
          | clvar, func |

          func = ins[mc:i_ffunction];

          clvar = lexists?(fn (var) [
            if (var[mc:v_cparent] == dvar)
              var
            else
              false;
          ], func[mc:c_fclosure]);

          if (!clvar) exit<function> null;

          if (!is_fn?)
            func[mc:c_fclosure] = ldelete!(clvar, func[mc:c_fclosure]);
          propagate_closure_constant(func, clvar, repvar, is_fn?);
        ]
      else
        fail();
    ];

  // is_fn? signals that the variable contains a local function
  // (closure) and we're going to annotate any call instructions with
  // the functions, so we can do type inference across the call
  propagate_closure_constant = fn (ifn, dvar, repvar, is_fn?)
    [
      | ndvar |
      ndvar = dvar[mc:v_number];

      graph_nodes_apply(fn (node) [
        node = graph_node_get(node);

        dforeach(fn (il) [
          if (bit_set?(il[mc:il_arguments], ndvar))
            [
              if (!is_fn? && !lfind?(ifn, remaining_fns))
                remaining_fns
                  = car(remaining_fns) . ifn . cdr(remaining_fns);
              replace_use(il, dvar, repvar, true, is_fn?);
            ]
        ], node[mc:f_ilist]);
      ], cdr(ifn[mc:c_fvalue]));
    ];

  | is_set_null? |
  is_set_null? = fn (ins)
    if (ins[mc:i_class] == mc:i_compute
        && ins[mc:i_aop] == mc:b_assign)
      [
        | var |
        var = car(ins[mc:i_aargs]);
        (var[mc:v_class] == mc:v_constant
         && var[mc:v_kvalue] == null)
      ]
    else
      false;

  // Constant-propagates single-assignment locals and that are read from
  // closures
  propagate_closure_constants = fn (ifn)
    [
      | vars |
      lforeach (fn (var) [
        | nodes, dilist, dins, vnum, value, uses, is_fn? |

        if (var[mc:v_class] != mc:v_local)
          exit<function> null;

        // Only propagate if the variable is closure_read.
        uses = var[mc:v_lclosure_uses];
        // For "regular" constants, propagate local_write_once.
        // For closures, propagate unless local_write_many_nonnull.
        if (uses & ~(mc:local_write_once | mc:local_write_many
                     | mc:closure_read))
          exit<function> null;

        nodes = graph_nodes(cdr(ifn[mc:c_fvalue]));
        vnum = var[mc:v_number];
        lexists?(fn (node) [
          node = graph_node_get(node);
          dexists?(fn (il) [
            if (il[mc:il_defined_var] == vnum)
              [
                dilist = il;
                // try to find a set-to-not-null instruction if
                // local_write_many (implies !local_write_many_nonnull)
                ((~uses & mc:local_write_many)
                 || !is_set_null?(il[mc:il_ins]))
              ]
            else
              false
          ], node[mc:f_ilist]);
        ], nodes);

        dins = dilist[mc:il_ins];

        is_fn? = false;
        if (dins[mc:i_class] == mc:i_closure)
          [
            is_fn? = true;
            value = dins[mc:i_ffunction];
          ]
        else if ((uses & mc:local_write_many)
                 || (~uses & mc:closure_read)
                 || dins[mc:i_class] != mc:i_compute
                 || dins[mc:i_aop] != mc:b_assign)
          exit<function> null
        else
          [
            value = car(dins[mc:i_aargs]);
            if (value[mc:v_class] != mc:v_constant
                && value[mc:v_class] != mc:v_global_constant)
              exit<function> null;
          ];

        vars = vector(var, is_fn?, value) . vars;
      ], ifn[mc:c_flocals_write]);

      if (vars == null) exit<function> false;

      lforeach (fn (pvar) [
        | dvar, is_fn?, repvar |

        @[dvar is_fn? repvar] = pvar;

        propagate_closure_constant(ifn, dvar, repvar, is_fn?);

        if (!is_fn?)
          dvar[mc:v_lclosure_uses] &= ~(mc:closure_read | mc:closure_write);
      ], vars);

      false
    ];

  propagate_copies = fn (f)
    // Misses copies that are born and die within a basic block
    // This would not seem to be a great problem, as phase2 generates an
    // extra temporary for assignments that stops this being a problem,
    // except for ambiguous variables (closures, globals), for which the
    // extra step is necessary anyway.
    [
      | propagate_copy, all_copies, globals |

      all_copies = mc:flow_copies(f);
      globals = mc:set_vars!(mc:new_varset(f), f[mc:c_fglobals]);
      mc:set_vars!(globals, f[mc:c_fclosure]);

      propagate_copy = fn (ncopy)
	[
	  | cblock, dvar, cins, repvar, cfirst, uses, local_uses, nrepvar,
	    replaceable_use?, can_repall, scan, ndvar, umap, copy, repuse |

          repuse = fn (use) replace_use(use, dvar, repvar, false, false);

	  replaceable_use? = fn (use)
	    [
	      | ublock, uins, first, uclass, ambvars |

	      uins = use[mc:il_ins];
	      if (uins == null) exit<function> true; // instruction was removed...
	      uclass = uins[mc:i_class];
	      ublock = graph_node_get(use[mc:il_node]);
	      first = ublock[mc:f_ilist];
	      ambvars = ublock[mc:f_ambiguous][mc:flow_out];

	      // Note: can't replace use in closures
	      if (uclass != mc:i_closure &&
                  uclass != mc:i_vref &&
		  bit_set?(ublock[mc:f_copies][mc:flow_in], ncopy) &&
		  !((uclass == mc:i_return ||
		     uclass == mc:i_call) && // escapes from function ?
		    (bit_set?(ambvars, ndvar) || bit_set?(globals, ndvar))))
		loop		// there must be no prior replacements
		  [
		    | fil, fins, fclass |

		    fil = dget(first);
		    fins = fil[mc:il_ins];
		    fclass = fins[mc:i_class];
		    if (fil == use) exit true;
		    // check if dvar or repvar is defined in this ins
		    // (no need to check explicit def of dvar, as in that case
		    // dvar would not be in 'uses').
		    if (fil[mc:il_defined_var] == nrepvar ||
			fclass == mc:i_call &&
			(bit_set?(ambvars, nrepvar) || bit_set?(globals, nrepvar) ||
			 bit_set?(ambvars, ndvar) || bit_set?(globals, ndvar)))
		      exit false;

		    first = dnext(first);
		  ]
	      else
		false
	    ];

	  copy = all_copies[ncopy];
	  cblock = graph_node_get(copy[mc:il_node]);
	  cins = copy[mc:il_ins];
	  dvar = cins[mc:i_adest];
	  ndvar = dvar[mc:v_number];
	  repvar = car(cins[mc:i_aargs]);
	  nrepvar = repvar[mc:v_number];
	  umap = cblock[mc:f_uses][mc:flow_map];

	  // Find all uses of this copy
	  // we know that the copy statement defines all uses of x
	  // which are available at the end of its block (otherwise
	  // it would not be a member of all_copies)

	  uses = breduce(fn (nuse, l)
			 [
			   | use |

			   use = umap[nuse];
			   if (car(use) == dvar) cdr(use) . l
			   else l
			 ], null, cblock[mc:f_uses][mc:flow_out]);

	  // add all local uses (in block of copy)
	  cfirst = scan = cblock[mc:f_ilist];
	  while (dget(scan) != copy) scan = dnext(scan);

	  can_repall = true;
	  // local uses may make complete replacement impossible
	  loop
	    [
	      | il, ins, class |

	      scan = dnext(scan);
	      if (scan == cfirst) exit 0;

	      il = dget(scan);
	      ins = il[mc:il_ins];
	      class = ins[mc:i_class];
	      if ((class == mc:i_closure || class == mc:i_vref) &&
		  bit_set?(il[mc:il_arguments], ndvar))
                can_repall = false
	      else if (((class == mc:i_return ||
			 class == mc:i_call) && // escapes from function ?
			(bit_set?(cblock[mc:f_ambiguous][mc:flow_out], ndvar) ||
			 bit_set?(globals, ndvar))))
		[
		  can_repall = false;
		  exit 0		// and no further uses are legal
		]
	      else if (bit_set?(il[mc:il_arguments], ndvar))
		local_uses = il . local_uses;
	    ];

	  // if repvar is a constant, propagate it anywhere it reaches
	  if (repvar[mc:v_class] == mc:v_constant)
	    [
	      lforeach(fn (use) if (replaceable_use?(use)) repuse(use), uses);
	      lforeach(repuse, local_uses);
	    ]
	  else if (can_repall && lforall?(replaceable_use?, uses)) // we can replace !
	    [
	      // replace all uses of dvar in uses by repvar
	      lforeach(repuse, uses);
	      lforeach(repuse, local_uses);
	    ]
	];

      for (|i| i = 0; i < vector_length(all_copies); ++i)
        propagate_copy(i)
    ];

  consttype = fn (arg)
    if (arg[mc:v_class] == mc:v_constant)
      typeof(arg[mc:v_kvalue])
    else if (arg[mc:v_class] == mc:v_global_constant)
      typeof(global_value(arg[mc:v_goffset]))
    else
      -1;

  convert_to_type_trap = fn (ins, arg, type)
    [
      ins[mc:i_class] = mc:i_trap;
      ins[mc:i_top] = mc:trap_type;
      ins[mc:i_tdest] = error_bad_type;
      ins[mc:i_targs] = list(arg, mc:var_make_constant(type));
    ];

  // Checks if "ins" has to be replaced by a type trap if removed by
  // the optimiser. If so, changes the instruction and returns true.
  check_compute_trap = fn (ins)
    [
      | ttype, cargs, op |

      if (ins[mc:i_class] != mc:i_compute)
        exit<function> false;

      op = ins[mc:i_aop];
      if (op != mc:b_assign)
        mc:warning("result of operation (%s) unused%s",
                   mc:builtin_names[op],
                   match (ins[mc:i_adest][mc:v_name]) [
                     "" => "";
                     s => format(" (in %s)", s)
                   ]);

      ttype = compute_trap_types[op];
      if (!ttype)
        exit<function> false;

      cargs = ins[mc:i_aargs];

      if (op == mc:b_add)
        [
          | ct |
          if ((ct = consttype(car(cargs))) >= 0)
            if (ct == type_integer || ct == type_string)
              convert_to_type_trap(ins, cadr(cargs), ct)
            else
              convert_to_type_trap(ins, car(cargs), type_integer)
          else if ((ct = consttype(cadr(cargs))) >= 0)
            if (ct == type_integer || ct == type_string)
              convert_to_type_trap(ins, car(cargs), ct)
            else
              convert_to_type_trap(ins, cadr(cargs), type_integer);
        ]
      else if (cdr(cargs) == null)              // unary op?
        if (consttype(car(cargs)) == ttype)
          exit<function> false
        else
          convert_to_type_trap(ins, car(cargs), ttype)
      else if (consttype(car(cargs)) == ttype)
        if (consttype(cadr(cargs)) == ttype)
          exit<function> false
        else
          convert_to_type_trap(ins, cadr(cargs), ttype)
      else if (consttype(cadr(cargs)) == ttype)
        convert_to_type_trap(ins, car(cargs), ttype);
      // otherwise, we have to test both arguments; can just as well
      // do the real operation...

      if (mc:verbose >= 3)
        [
          dformat("Trap prevent USELESS: %w\n", ins);
        ];

      true
    ];

  useless_instructions = fn (ifn, globals)
    [
      | useless |

      graph_nodes_apply
	(fn (n)
	 [
	   | block, uses, ilist, scan, defined, amblist, local_uses, vmap,
             closure_uses |

	   block = graph_node_get(n);
	   uses = bitset_to_list(block[mc:f_uses][mc:flow_out],
				 block[mc:f_uses][mc:flow_map]);
	   ilist = block[mc:f_ilist];

	   // Precompute ambiguous information for each
	   // instruction, in reverse order
	   amblist = mc:scan_ambiguous(fn (il, ambiguous, amblist)
				       ambiguous . amblist, null,
				       block, globals, mc:closure_write);

	   defined = mc:new_varset(ifn);
	   vmap = ifn[mc:c_fallvars];
	   scan = ilist;

	   closure_uses = mc:new_varset(ifn);
           lforeach(fn (var) [
             if (var[mc:v_class] == mc:v_local &&
                 (var[mc:v_lclosure_uses] & (mc:closure_read | mc:closure_write)))
               set_bit!(closure_uses, var[mc:v_number]);
           ], ifn[mc:c_flocals]);
           local_uses = bcopy(closure_uses);

	   loop
	     [
	       | il, ins, class, args, ndvar |

	       scan = dprev(scan);
	       il = dget(scan);
	       ins = il[mc:il_ins];
	       class = ins[mc:i_class];

               mc:lineno = il[mc:il_lineno];

	       ndvar = il[mc:il_defined_var];
	       args = mc:barguments(il, car(amblist));
	       amblist = cdr(amblist);

	       if (class == mc:i_branch &&
		   ins[mc:i_bop] == mc:branch_never ||

		   class == mc:i_trap &&
		   ins[mc:i_top] == mc:trap_never ||

		   (class == mc:i_compute && // a = a is useless
		    ins[mc:i_aop] == mc:b_assign &&
		    ins[mc:i_adest] == car(ins[mc:i_aargs])) ||

		   (class == mc:i_compute ||
		    class == mc:i_memory && ins[mc:i_mop] == mc:memory_read) &&
		   bit_clear?(local_uses, ndvar) &&
		   (bit_set?(defined, ndvar) || !assq(vmap[ndvar], uses)))
                 if (!check_compute_trap(ins))
                   useless = scan . useless;

	       if (ndvar)
		 [
		   set_bit!(defined, ndvar);
		   clear_bit!(local_uses, ndvar);
		 ];

	       if (class != mc:i_closure)
                 bunion!(local_uses, args);

	       if (class == mc:i_return ||
                   (class == mc:i_call && mc:call_escapes?(ins)))
                 bunion!(local_uses, closure_uses);

	       if (scan == ilist) exit 0;
	     ];
	 ], cdr(ifn[mc:c_fvalue]));
      useless
    ];

  remove_instruction = fn (f, ilpos)
    [
      | il, block, first, nblock, olabel |

      il = dget(ilpos);
      il[mc:il_ins] = null; // note removal of instruction
      block = il[mc:il_node];
      first = graph_node_get(block)[mc:f_ilist];

      if (dnext(ilpos) != ilpos) // remains more than one instruction
	[
	  if (ilpos == first) // move second instruction up
	    [
	      | second, il2 |

	      second = dnext(first);
              il2 = dget(second);
	      il[mc:il_ins] = il2[mc:il_ins];
              il[mc:il_lineno] = il2[mc:il_lineno];
	      dremove!(second, second)
	    ]
	  else
	    dremove!(ilpos, ilpos)
	]
      else
	[
	  // find fall through block
	  graph_edges_out_apply(fn (e) if (graph_edge_get(e))
				nblock = graph_edge_to(e),
				block);
	  assert(nblock != null);

	  // if block was entry block, change
	  if (car(f[mc:c_fvalue]) == block)
	    set_car!(f[mc:c_fvalue], nblock);

	  // edges into block becomes edges into nblock
	  lforeach(fn (e)
		   [
		     graph_add_edge(graph_edge_from(e), nblock, graph_edge_get(e));
		     graph_remove_edge(e)
		   ], graph_edges_in(block));

	  // set label (if any) to first instruction of nblock
	  if (olabel = il[mc:il_label])
	    mc:set_label(olabel, dget(graph_node_get(nblock)[mc:f_ilist]));
	]
    ];

  eliminate_dead_code = fn (f)
    [
      | fg, entry, useless, fval |

      // assumes that edges removed because of branch changes
      // have already been handled

      // remove:
      //   useless branches
      //   useless traps
      //   unused definitions

      // as a result:
      //   some labels may become unused
      //   some blocks may be mergeable (not done for now)
      //   some blocks may disappear (no instructions left)
      //   some blocks may be unreachable

      fval = f[mc:c_fvalue];
      fg = cdr(fval);

      useless = useless_instructions
	(f, mc:set_vars!(mc:set_vars!(mc:new_varset(f), f[mc:c_fglobals_write]),
			 f[mc:c_fclosure_write]));
      if (useless != null)
	[
	  if (mc:verbose >= 3)
	    [
	      dformat("USELESS %s\n",
                      concat_words(lmap(fn (i) itoa(dget(i)[mc:il_number]),
                                        useless),
                                   " "));
	    ];
	  change = true;
	];
      lforeach(fn (ins) remove_instruction(f, ins), useless);

      entry = car(fval);
      // remove useless nodes (never the entry node!)
      lforeach(fn (n) if (n != entry && graph_edges_in(n) == null)
	       [
		 // unreachable
		 change = true;
		 if (mc:verbose >= 3)
		   [
		     display("UNREACHABLE\n");
		   ];
		 lforeach(fn (e) graph_remove_edge(e), graph_edges_out(n));
		 graph_remove_node(n)
	       ], graph_nodes(fg));

      // remove aliases and useless labels
      graph_nodes_apply(fn (n) mc:remove_aliases(graph_node_get(n)[mc:f_ilist]), fg);
      graph_nodes_apply(fn (n) mc:remove_labels(graph_node_get(n)[mc:f_ilist]), fg);
    ];

  mc:recompute_vars = fn (ifn, dglobals?)
    // Types: ifn : intermediate function, dglobals?: boolean
    // Effects: Recomputes the variable lists (locals, closures, globals) of
    //   ifn (may have changed because of optimisation)
    //   Also renumbers these variables from 1 (for use in bitmasks)
    //   Numbers dglobals if dglobals? is true
    [
      | locals, closures, globals, vars_read, vars_written, args_ins,
        ins_escapes?, allvars,
	defined, wlocals, wnonnulllocals, wclosures, wglobals, vindex,
        dglobals_read |

      vars_written = fn (il)
	[
	  | ins, dvar, class, new |

	  ins = il[mc:il_ins];

	  dvar = mc:defined_var(ins);
	  if (dvar)
	    [
	      new = false;
	      class = dvar[mc:v_class];
	      if (class == mc:v_global)
		[
		  if (!memq(dvar, wglobals)) new = wglobals = dvar . wglobals
		]
	      else if (class == mc:v_local)
		[
		  if (!memq(dvar, wlocals))
                    [
                      | ouse |
                      new = wlocals = dvar . wlocals;
                      ouse = dvar[mc:v_lclosure_uses];
                      ouse &= ~(mc:local_write_once | mc:local_write_many
                                | mc:local_write_many_nonnull);

                      if (lfind?(dvar, ifn[mc:c_fargs]))
                        dvar[mc:v_lclosure_uses]
                          = (ouse | mc:local_write_many
                             | mc:local_write_many_nonnull)
                      else
                        dvar[mc:v_lclosure_uses] = ouse | mc:local_write_once;
                    ]
                  else
                    dvar[mc:v_lclosure_uses] |= mc:local_write_many;

                  if ((~dvar[mc:v_lclosure_uses] & mc:local_write_many_nonnull)
                      && !is_set_null?(ins))
                    if (!memq(dvar, wnonnulllocals))
                      wnonnulllocals = dvar . wnonnulllocals
                    else
                      dvar[mc:v_lclosure_uses] |= mc:local_write_many_nonnull;
		]
	      else if (class == mc:v_closure)
		[
		  if (!memq(dvar, wclosures)) new = wclosures = dvar . wclosures
		];

	      if (new)
		[
		  dvar[mc:v_number] = vindex = vindex + 1;
		  allvars = dvar . allvars
		];
	      il[mc:il_defined_var] = dvar[mc:v_number];
	    ]
	  else
	    il[mc:il_defined_var] = false;

	];

      vars_read = fn (il)
	[
	  | ins, args |

	  ins = il[mc:il_ins];

	  args = mc:arguments(ins, null);
	  lforeach(fn (v)
		   [
		     | class, new |

		     class = v[mc:v_class];
		     new = false;
		     if (class == mc:v_global)
		       [
			 if (!memq(v, globals)) new = globals = v . globals
		       ]
		     else if (class == mc:v_local)
		       [
			 if (!memq(v, locals)) new = locals = v . locals
		       ]
		     else if (class == mc:v_closure)
		       [
			 if (!memq(v, closures)) new = closures = v . closures
		       ]
		     else
		       v[mc:v_number] = 0;

		     if (new)
		       [
			 v[mc:v_number] = vindex = vindex + 1;
			 allvars = v . allvars
		       ];
		   ], args);
	  il[mc:il_arguments] = args;
	];

      dglobals_read = fn (il)
	lforeach(fn (v)
		 if (v[mc:v_class] == mc:v_global_define &&
		     !memq(v, globals))
		 [
		   globals = v . globals;
		   v[mc:v_number] = vindex = vindex + 1;
		   allvars = v . allvars;
		 ],
		 il[mc:il_arguments]);

      args_ins = fn (il)
	[
	  | args, ndvar |

	  args = mc:new_varset(ifn);
	  il[mc:il_arguments] = mc:set_vars!(args, il[mc:il_arguments]);
	  clear_bit!(args, 0); // remove ref to constants

	  if (ndvar = il[mc:il_defined_var])
	    set_bit!(defined, ndvar);
	];

      ins_escapes? = fn (il)
        [
          | ins |
          ins = il[mc:il_ins];
          (ins[mc:i_class] == mc:i_call && mc:call_escapes?(ins))
        ];

      // clear the index of argument variables (so that unused arguments
      // are easily ignored)
      lforeach(fn (arg) arg[mc:v_number] = 0, ifn[mc:c_fargs]);

      // first find all variables that are written
      // then prepend those that are read (but not written)
      // the whole list, and the sublist of written vars are saved
      vindex = 0;
      graph_nodes_apply(fn (n) dforeach(vars_written, graph_node_get(n)[mc:f_ilist]),
			cdr(ifn[mc:c_fvalue]));
      locals = wlocals; closures = wclosures; globals = wglobals;
      graph_nodes_apply(fn (n) dforeach(vars_read, graph_node_get(n)[mc:f_ilist]),
			cdr(ifn[mc:c_fvalue]));
      if (dglobals?)
	graph_nodes_apply(fn (n) dforeach(dglobals_read, graph_node_get(n)[mc:f_ilist]),
			  cdr(ifn[mc:c_fvalue]));

      ifn[mc:c_flocals] = locals;
      ifn[mc:c_fglobals] = globals;
      ifn[mc:c_fclosure] = closures;
      ifn[mc:c_flocals_write] = wlocals;
      ifn[mc:c_fglobals_write] = wglobals;
      ifn[mc:c_fclosure_write] = wclosures;
      ifn[mc:c_fnvars] = vindex + 1;
      ifn[mc:c_fallvars] = list_to_vector(null . lreverse!(allvars));

      | escapes |
      escapes = wclosures != null || wglobals != null;
      graph_nodes_apply
	(fn (n)
	 [
	   | block |

	   block = graph_node_get(n);
           block[mc:f_dvars] = defined = mc:new_varset(ifn);
	   dforeach(args_ins, block[mc:f_ilist]);
           escapes ||= dexists?(ins_escapes?, block[mc:f_ilist]);
	 ], cdr(ifn[mc:c_fvalue]));
      ifn[mc:c_fnoescape] = !escapes;
    ];

  optimise_function = fn (f)
    [
      mc:this_function = f;
      if (mc:verbose >= 2)
	[
	  dformat("Optimising %s\n", mc:fname(f));
	];
      change = true;
      while (change)
	<continue> [
	  change = false;

	  // fold constants in all basic blocks
	  graph_nodes_apply
	    (fn (n)
	       dforeach(fold_constants, graph_node_get(n)[mc:f_ilist]),
	     cdr(f[mc:c_fvalue]));

	  // compute basic information
	  mc:recompute_vars(f, false);
          if (propagate_closure_constants(f))
            [
              change = true;
              exit<continue> null;
            ];
	  mc:flow_ambiguous(f, mc:closure_write);
	  mc:flow_uses(f);

	  eliminate_dead_code(f);
	  propagate_copies(f);
	];
      // clear data-flow information
      mc:clear_dataflow(f);
      mc:this_function = null;
    ];

  mc:optimise_functions = fn (fns)
    [
      remaining_fns = fns;
      while (remaining_fns != null)
        [
          optimise_function(car(remaining_fns));
          remaining_fns = cdr(remaining_fns);
        ];
    ];

];
