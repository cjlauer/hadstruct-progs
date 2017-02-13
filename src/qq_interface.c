#include <qhg.h>
#include <parser_types.h>
#include <quda.h>
#include <qq_interface_types.h>

#define MAX(a, b) (a) > (b) ? (a) : (b)

static int q_dims[ND];
static int q_procs[ND];
static int q_ldims[ND];
static int proc_dims[ND];
static int ldims[ND];
static double *h_gauge[ND];
static int vol, lvol;
static double *aux_spinor[2];

static int
rank_mapper(const int *c, void *args)
{
  MPI_Comm *comm = ((MPI_Comm *)(args));
  int t = c[3];
  int z = c[2];
  int y = c[1];
  int x = c[0];

  int co[] = {t, x, y, z};
  int rank;
  MPI_Cart_rank(*comm, co, &rank);
  return rank;
}

static void
cnvrt_gauge_field(double *h_g[ND], qhg_gauge_field gauge, enum qhg_fermion_bc_time bc)
{
  int apply_bc = 0;
  double fct = 1.0;
  switch(bc) {
  case PERIODIC:
    fct = +1.0;
    break;
  case ANTIPERIODIC:
    fct = -1.0;
    break;
  }
  int tid = gauge.lat->comms->proc_coords[0];
  int ntp = gauge.lat->comms->proc_dims[0];
  for(int t=0; t<q_ldims[3]; t++) {
    if((t == q_ldims[3]-1) && (tid == ntp-1)) {
      apply_bc = 1;
    } else {
      apply_bc = 0;
    }
    for(int z=0; z<q_ldims[2]; z++)
      for(int y=0; y<q_ldims[1]; y++)
	for(int x=0; x<q_ldims[0]; x++) {
	  int idx0 = z + ldims[3]*(y + ldims[2]*(x + ldims[1]*t));
	  int idx1 = x + q_ldims[0]*(y + q_ldims[1]*(z + q_ldims[2]*t));
	  int eo = (x+y+z+t) % 2;
	  for(int mu=0; mu<ND; mu++) {
	    int nu = (ND+mu-1) % ND;
	    double *g0 = (double *)&(gauge.field[NC*NC*(mu + ND*idx0)]);
	    double *g1 = &(h_g[nu][NC*NC*(2*(eo*(lvol/2) + (idx1/2)))]);
	    memcpy(g1, g0, sizeof(double)*NC*NC*2);
	  }
	  if(apply_bc) {
	    double *g1 = &(h_g[3][NC*NC*(2*(eo*(lvol/2) + (idx1/2)))]);
	    for(int i=0; i<NC*NC*2; i++)
	      g1[i] *= fct;
	  }
	}
  }
  return;
}

static void
cnvrt_spinor_field_to_quda(double *sp1, qhg_spinor_field sp0)
{
  for(int t=0; t<q_ldims[3]; t++)
    for(int z=0; z<q_ldims[2]; z++)
      for(int y=0; y<q_ldims[1]; y++)
	for(int x=0; x<q_ldims[0]; x++) {
	  int idx0 = z + ldims[3]*(y + ldims[2]*(x + ldims[1]*t));
	  int idx1 = x + q_ldims[0]*(y + q_ldims[1]*(z + q_ldims[2]*t));
	  int eo = (x+y+z+t) % 2;
	  double *s0 = (double *)&(sp0.field[NC*NS*idx0]);
	  double *s1 = &(sp1[NC*NS*(2*(eo*(lvol/2) + (idx1/2)))]);
	  // memcpy(s1, s0, sizeof(double)*NS*NC*2);
	  /*
	   * Multiply by gamma_y (gamma basis transformation from tmLQCD to DeGrand-Rossi)
	   */
	  s1[CS(0,0)*2+0] = -s0[CS(3,0)*2+0];
	  s1[CS(0,0)*2+1] = -s0[CS(3,0)*2+1];
	  s1[CS(0,1)*2+0] = -s0[CS(3,1)*2+0];
	  s1[CS(0,1)*2+1] = -s0[CS(3,1)*2+1];
	  s1[CS(0,2)*2+0] = -s0[CS(3,2)*2+0];
	  s1[CS(0,2)*2+1] = -s0[CS(3,2)*2+1];

	  s1[CS(1,0)*2+0] =  s0[CS(2,0)*2+0];
	  s1[CS(1,0)*2+1] =  s0[CS(2,0)*2+1];
	  s1[CS(1,1)*2+0] =  s0[CS(2,1)*2+0];
	  s1[CS(1,1)*2+1] =  s0[CS(2,1)*2+1];
	  s1[CS(1,2)*2+0] =  s0[CS(2,2)*2+0];
	  s1[CS(1,2)*2+1] =  s0[CS(2,2)*2+1];

	  s1[CS(2,0)*2+0] =  s0[CS(1,0)*2+0];
	  s1[CS(2,0)*2+1] =  s0[CS(1,0)*2+1];
	  s1[CS(2,1)*2+0] =  s0[CS(1,1)*2+0];
	  s1[CS(2,1)*2+1] =  s0[CS(1,1)*2+1];
	  s1[CS(2,2)*2+0] =  s0[CS(1,2)*2+0];
	  s1[CS(2,2)*2+1] =  s0[CS(1,2)*2+1];

	  s1[CS(3,0)*2+0] = -s0[CS(0,0)*2+0];
	  s1[CS(3,0)*2+1] = -s0[CS(0,0)*2+1];
	  s1[CS(3,1)*2+0] = -s0[CS(0,1)*2+0];
	  s1[CS(3,1)*2+1] = -s0[CS(0,1)*2+1];
	  s1[CS(3,2)*2+0] = -s0[CS(0,2)*2+0];
	  s1[CS(3,2)*2+1] = -s0[CS(0,2)*2+1];
	}
  return;
}

static void
cnvrt_spinor_field_from_quda(qhg_spinor_field sp1, double *sp0)
{
  for(int t=0; t<q_ldims[3]; t++)
    for(int z=0; z<q_ldims[2]; z++)
      for(int y=0; y<q_ldims[1]; y++)
	for(int x=0; x<q_ldims[0]; x++) {
	  int idx0 = x + q_ldims[0]*(y + q_ldims[1]*(z + q_ldims[2]*t));
	  int idx1 = z + ldims[3]*(y + ldims[2]*(x + ldims[1]*t));
	  int eo = (x+y+z+t) % 2;
	  double *s0 = &(sp0[NC*NS*(2*(eo*(lvol/2) + (idx0/2)))]);
	  double *s1 = (double *)&(sp1.field[NC*NS*idx1]);
	  //memcpy(s1, s0, sizeof(double)*NS*NC*2);
	  /*
	   * Multiply by gamma_y (gamma basis transformation from tmLQCD to DeGrand-Rossi)
	   */
	  s1[CS(0,0)*2+0] = -s0[CS(3,0)*2+0];
	  s1[CS(0,0)*2+1] = -s0[CS(3,0)*2+1];
	  s1[CS(0,1)*2+0] = -s0[CS(3,1)*2+0];
	  s1[CS(0,1)*2+1] = -s0[CS(3,1)*2+1];
	  s1[CS(0,2)*2+0] = -s0[CS(3,2)*2+0];
	  s1[CS(0,2)*2+1] = -s0[CS(3,2)*2+1];

	  s1[CS(1,0)*2+0] =  s0[CS(2,0)*2+0];
	  s1[CS(1,0)*2+1] =  s0[CS(2,0)*2+1];
	  s1[CS(1,1)*2+0] =  s0[CS(2,1)*2+0];
	  s1[CS(1,1)*2+1] =  s0[CS(2,1)*2+1];
	  s1[CS(1,2)*2+0] =  s0[CS(2,2)*2+0];
	  s1[CS(1,2)*2+1] =  s0[CS(2,2)*2+1];

	  s1[CS(2,0)*2+0] =  s0[CS(1,0)*2+0];
	  s1[CS(2,0)*2+1] =  s0[CS(1,0)*2+1];
	  s1[CS(2,1)*2+0] =  s0[CS(1,1)*2+0];
	  s1[CS(2,1)*2+1] =  s0[CS(1,1)*2+1];
	  s1[CS(2,2)*2+0] =  s0[CS(1,2)*2+0];
	  s1[CS(2,2)*2+1] =  s0[CS(1,2)*2+1];

	  s1[CS(3,0)*2+0] = -s0[CS(0,0)*2+0];
	  s1[CS(3,0)*2+1] = -s0[CS(0,0)*2+1];
	  s1[CS(3,1)*2+0] = -s0[CS(0,1)*2+0];
	  s1[CS(3,1)*2+1] = -s0[CS(0,1)*2+1];
	  s1[CS(3,2)*2+0] = -s0[CS(0,2)*2+0];
	  s1[CS(3,2)*2+1] = -s0[CS(0,2)*2+1];
	}
  return;
}

qq_state
qq_init(struct run_params rp, qhg_gauge_field gf, enum qhg_fermion_bc_time bc)
{
  int am_io_proc = gf.lat->comms->proc_id == 0 ? 1 : 0;

  proc_dims[0] = gf.lat->comms->proc_dims[0];
  proc_dims[1] = gf.lat->comms->proc_dims[1];
  proc_dims[2] = gf.lat->comms->proc_dims[2];
  proc_dims[3] = gf.lat->comms->proc_dims[3];

  ldims[0] = gf.lat->ldims[0];
  ldims[1] = gf.lat->ldims[1];
  ldims[2] = gf.lat->ldims[2];
  ldims[3] = gf.lat->ldims[3];
  
  q_dims[3] = gf.lat->dims[0];
  q_dims[2] = gf.lat->dims[3];
  q_dims[1] = gf.lat->dims[2];
  q_dims[0] = gf.lat->dims[1];  

  q_ldims[3] = ldims[0];
  q_ldims[2] = ldims[3];
  q_ldims[1] = ldims[2];
  q_ldims[0] = ldims[1];  
  
  q_procs[3] = proc_dims[0];
  q_procs[2] = proc_dims[3];
  q_procs[1] = proc_dims[2];
  q_procs[0] = proc_dims[1];  

  vol = 1;
  lvol = 1;
  for(int i=0; i<ND; i++) {
    vol *= q_dims[i];
    lvol *= q_ldims[i];
  }
  
  initCommsGridQuda(ND, q_procs, &rank_mapper, (void *)&(gf.lat->comms->comm));
  initQuda(-1);

  qq_state state;
  state.gauge_param = newQudaGaugeParam();
  for(int i=0; i<ND; i++)
    state.gauge_param.X[i] = q_ldims[i];
  state.gauge_param.anisotropy = 1.0;
  state.gauge_param.type = QUDA_WILSON_LINKS;
  state.gauge_param.gauge_order = QUDA_QDP_GAUGE_ORDER;  
  switch(bc) {
  case PERIODIC:
    state.gauge_param.t_boundary = QUDA_PERIODIC_T;
    break;
  case ANTIPERIODIC:
    state.gauge_param.t_boundary = QUDA_ANTI_PERIODIC_T;
    break;
  }
  
  state.gauge_param.cpu_prec = 8;
  state.gauge_param.cuda_prec = 8;
  state.gauge_param.cuda_prec_sloppy = 4;
  
  state.gauge_param.reconstruct = QUDA_RECONSTRUCT_NO;
  state.gauge_param.reconstruct_sloppy = QUDA_RECONSTRUCT_12;

  state.gauge_param.cuda_prec_precondition = 4;
  state.gauge_param.reconstruct_precondition = QUDA_RECONSTRUCT_12;
  
  state.gauge_param.ga_pad = (MAX(q_ldims[3]*q_ldims[2]*q_ldims[1],
				  MAX(q_ldims[3]*q_ldims[2]*q_ldims[0],
				      MAX(q_ldims[3]*q_ldims[0]*q_ldims[1],
					  q_ldims[0]*q_ldims[2]*q_ldims[1]))))/2;
  state.gauge_param.gauge_fix = QUDA_GAUGE_FIXED_NO;
  
  size_t bytes = lvol*NC*NC*sizeof(double)*2;
  for(int mu=0; mu<ND; mu++)
    h_gauge[mu] = qhg_alloc(bytes);

  cnvrt_gauge_field(h_gauge, gf, bc);  
  loadGaugeQuda(h_gauge, &state.gauge_param);  
  
  double plaq[3];
  plaqQuda(plaq);
  if(am_io_proc)
    printf("Plaquette according to QUDA = %12.10f\n", plaq[0]);
  
  state.invert_param = newQudaInvertParam();
  state.invert_param.tune = QUDA_TUNE_YES;
  state.invert_param.Ls = 1;
  state.invert_param.dslash_type = QUDA_TWISTED_CLOVER_DSLASH;
  state.invert_param.mu = rp.act.mu;
  state.invert_param.kappa = rp.act.kappa;
  state.invert_param.mass = 1./(2.*state.invert_param.kappa) - 4.0;
  state.invert_param.inv_type = QUDA_GCR_INVERTER;
  state.invert_param.inv_type_precondition = QUDA_MG_INVERTER;
  state.invert_param.solution_type = QUDA_MAT_SOLUTION;
  state.invert_param.matpc_type = QUDA_MATPC_EVEN_EVEN;
  state.invert_param.dagger = QUDA_DAG_NO;
  state.invert_param.mass_normalization = QUDA_MASS_NORMALIZATION;
  state.invert_param.solver_normalization = QUDA_DEFAULT_NORMALIZATION;
  state.invert_param.solve_type = QUDA_DIRECT_PC_SOLVE;
  state.invert_param.pipeline = 0;
  state.invert_param.tol_restart = 1e-2;
  state.invert_param.tol = 1e-7;
  state.invert_param.residual_type = QUDA_L2_RELATIVE_RESIDUAL;
  state.invert_param.maxiter = 100000;
  state.invert_param.reliable_delta = 5e-1;
  state.invert_param.use_sloppy_partial_accumulator = 0;
  state.invert_param.max_res_increase = 1;

  state.invert_param.cpu_prec = 8;
  state.invert_param.cuda_prec = 8;
  state.invert_param.cuda_prec_sloppy = 4;
  state.invert_param.cuda_prec_precondition = 4;
  state.invert_param.preserve_source = QUDA_PRESERVE_SOURCE_YES;
  state.invert_param.gamma_basis = QUDA_DEGRAND_ROSSI_GAMMA_BASIS;
  state.invert_param.dirac_order = QUDA_DIRAC_ORDER;
  state.invert_param.input_location = QUDA_CPU_FIELD_LOCATION;
  state.invert_param.output_location = QUDA_CPU_FIELD_LOCATION;

  state.invert_param.clover_cpu_prec = 8;
  state.invert_param.clover_cuda_prec = 8;
  state.invert_param.clover_cuda_prec_sloppy = 4;
  state.invert_param.clover_cuda_prec_precondition = 4;
  state.invert_param.clover_order = QUDA_PACKED_CLOVER_ORDER;
  state.invert_param.clover_coeff = rp.act.csw*state.invert_param.kappa;
  state.invert_param.verbosity = QUDA_SUMMARIZE; //QUDA_DEBUG_VERBOSE;
  state.invert_param.verbosity_precondition = QUDA_SUMMARIZE; //QUDA_DEBUG_VERBOSE;
  state.invert_param.compute_clover = 1;
  state.invert_param.return_clover = 0;
  state.invert_param.compute_clover_inverse = 1;
  state.invert_param.return_clover_inverse = 0;
  state.invert_param.cl_pad = 0;
  state.invert_param.sp_pad = 0;

  state.invert_param.Nsteps = 20;
  state.invert_param.gcrNkrylov = 10;
  
  state.mg_param = newQudaMultigridParam();
  QudaInvertParam mg_invert_param = newQudaInvertParam();
  mg_invert_param = state.invert_param;
  state.mg_param.invert_param = &mg_invert_param;
  state.mg_param.invert_param->solve_type = QUDA_DIRECT_SOLVE;
  state.mg_param.invert_param->inv_type_precondition = QUDA_INVALID_INVERTER;

  
  double delta_muPR = 6.0;
  double delta_muCG = 1.0;
  double delta_kappaPR = 1.0;
  double delta_kappaCG = 1.0;

  state.mg_param.delta_muPR = delta_muPR;
  state.mg_param.delta_muCG = delta_muCG;
  state.mg_param.delta_kappaPR = delta_kappaPR;
  state.mg_param.delta_kappaCG = delta_kappaCG;
  state.mg_param.n_level = 2;

  int geo_block_size[2][ND] = {{4, 4, 4, 4},
			       {1, 1, 1, 1}};
  
  for (int i=0; i<state.mg_param.n_level; i++) {
    for (int j=0; j<ND; j++) {
      state.mg_param.geo_block_size[i][j] = geo_block_size[i][j];
    }
    state.mg_param.spin_block_size[i] = 1;
    
    state.mg_param.n_vec[i] = 24;
    state.mg_param.nu_pre[i] = 0;
    state.mg_param.nu_post[i] = 3;
    
    state.mg_param.cycle_type[i] = QUDA_MG_CYCLE_RECURSIVE;
    
    state.mg_param.smoother[i] = QUDA_MR_INVERTER;

    state.mg_param.setup_maxiter = 3;
    state.mg_param.setup_tol = 1e-1;

    state.mg_param.smoother_tol[i] = 1e-3;
    state.mg_param.global_reduction[i] = QUDA_BOOLEAN_YES;

    state.mg_param.smoother_solve_type[i] = QUDA_DIRECT_PC_SOLVE; // EVEN-ODD

    // if we are using an outer even-odd preconditioned solve, then we
    // use single parity injection into the coarse grid
    state.mg_param.coarse_grid_solution_type[i] = QUDA_MATPC_SOLUTION;

    state.mg_param.omega[i] = 0.85; // over/under relaxation factor

    state.mg_param.location[i] = QUDA_CUDA_FIELD_LOCATION;
  }

  // only coarsen the spin on the first restriction
  state.mg_param.spin_block_size[0] = 2;

  // coarse grid solver is GCR
  state.mg_param.smoother[state.mg_param.n_level-1] = QUDA_GCR_INVERTER;
  state.mg_param.compute_null_vector = QUDA_COMPUTE_NULL_VECTOR_YES;
  state.mg_param.generate_all_levels = QUDA_BOOLEAN_YES;  
  state.mg_param.run_verify = QUDA_BOOLEAN_NO;

  strcpy(state.mg_param.vec_outfile, "");
  strcpy(state.mg_param.vec_infile, "");

  loadCloverQuda(NULL, NULL, &state.invert_param);
  state.mg_param.invert_param->twist_flavor = QUDA_TWIST_PLUS;
  state.invert_param.preconditionerUP = newMultigridQuda(&state.mg_param);
  state.mg_param.invert_param->twist_flavor = QUDA_TWIST_MINUS;
  state.invert_param.preconditionerDN = newMultigridQuda(&state.mg_param);
  
  bytes = sizeof(double)*NS*NC*lvol*2;
  aux_spinor[0] = qhg_alloc(bytes);
  aux_spinor[1] = qhg_alloc(bytes);
  
  return state;
}

void
qq_invert(qhg_spinor_field out, qhg_spinor_field in, double eps, enum mu_sign s, qq_state *state)
{
  state->invert_param.tol = eps;
  state->invert_param.twist_flavor = s == up ? QUDA_TWIST_MINUS : QUDA_TWIST_PLUS;
  state->invert_param.preconditioner = s == up ? state->invert_param.preconditionerDN : state->invert_param.preconditionerUP;
  cnvrt_spinor_field_to_quda(aux_spinor[0], in);
  invertQuda(aux_spinor[1], aux_spinor[0], &state->invert_param);
  cnvrt_spinor_field_from_quda(out, aux_spinor[1]);
  
  return;
}

void
qq_finalize(qq_state state)
{
  destroyMultigridQuda(state.invert_param.preconditionerUP);
  destroyMultigridQuda(state.invert_param.preconditionerDN);
  freeCloverQuda();
  freeGaugeQuda();  
  endQuda();
  for(int i=0; i<ND; i++)
    free(h_gauge[i]);

  free(aux_spinor[0]);
  free(aux_spinor[1]);
  return;
}

