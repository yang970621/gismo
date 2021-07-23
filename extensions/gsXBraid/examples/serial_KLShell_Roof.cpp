/** @file gsThinShell_ArcLength.cpp

    @brief Code for the arc-length method of a shell based on loads

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): H.M. Verhelst (2019-..., TU Delft)
*/

#include <gismo.h>

#include <gsKLShell/gsThinShellAssembler.h>
#include <gsKLShell/getMaterialMatrix.h>
// #include <gsThinShell/gsArcLengthIterator.h>
#include <gsStructuralAnalysis/gsArcLengthIterator.h>

using namespace gismo;

template <class T>
void initStepOutput( const std::string name, const gsMatrix<T> & points);

template <class T>
void writeStepOutput(const gsArcLengthIterator<T> & arcLength, const gsMultiPatch<T> & deformation, const std::string name, const gsMatrix<T> & points, const index_t extreme=-1, const index_t kmax=100);

int main (int argc, char** argv)
{
    // Input options
    int numElevate    = 1;
    int numHref       = 1;
    bool plot         = false;
    bool mesh         = false;
    bool stress       = false;
    bool membrane     = false;
    bool quasiNewton  = false;
    int quasiNewtonInt= -1;
    bool adaptive     = false;
    int step          = 10;
    int method        = 2; // (0: Load control; 1: Riks' method; 2: Crisfield's method; 3: consistent crisfield method; 4: extended iterations)
    bool deformed     = false;

    bool composite = false;

    real_t relax      = 1.0;

    int testCase      = 0;

    int result        = 0;

    bool write        = false;

    index_t maxit     = 20;
    // index_t iniLevels  = 2;
    // index_t maxLevels  = 4;
    index_t maxLevel  = 2;

    // Arc length method options
    real_t dL        = 0.5; // Ard length to find bifurcation
    real_t tol        = 1e-6;
    real_t tolU       = 1e-6;
    real_t tolF       = 1e-3;

    std::string wn("data.csv");

    std::string assemberOptionsFile("options/solver_options.xml");

    gsCmdLine cmd("Arc-length analysis for thin shells.");
    cmd.addString( "f", "file", "Input XML file for assembler options", assemberOptionsFile );

    cmd.addInt("t", "testcase", "Test case: 0: clamped-clamped, 1: pinned-pinned, 2: clamped-free", testCase);

    cmd.addInt("r","hRefine", "Number of dyadic h-refinement (bisection) steps to perform before solving", numHref);
    cmd.addInt("e","degreeElevation", "Number of degree elevation steps to perform on the Geometry's basis before solving", numElevate);
    cmd.addSwitch("composite", "Composite material", composite);

    cmd.addInt("m","Method", "Arc length method; 1: Crisfield's method; 2: RIks' method.", method);
    cmd.addReal("L","dL", "arc length", dL);
    // cmd.addInt("I","inilvl", "Initial levels", iniLevels);
    // cmd.addInt("M","maxlvl", "Max levels", maxLevels);
    cmd.addInt("l","level", "Max level", maxLevel);
    cmd.addReal("A","relaxation", "Relaxation factor for arc length method", relax);

    cmd.addInt("q","QuasiNewtonInt","Use the Quasi Newton method every INT iterations",quasiNewtonInt);
    cmd.addInt("N", "maxsteps", "Maximum number of steps", step);

    cmd.addSwitch("adaptive", "Adaptive length ", adaptive);
    cmd.addSwitch("quasi", "Use the Quasi Newton method", quasiNewton);
    cmd.addSwitch("plot", "Plot result in ParaView format", plot);
    cmd.addSwitch("mesh", "Plot mesh?", mesh);
    cmd.addSwitch("stress", "Plot stress in ParaView format", stress);
    cmd.addSwitch("membrane", "Use membrane model (no bending)", membrane);
    cmd.addSwitch("deformed", "plot on deformed shape", deformed);
    cmd.addSwitch("write", "write to file", write);

    try { cmd.getValues(argc,argv); } catch (int rv) { return rv; }

    // GISMO_ASSERT(maxLevels>iniLevels,"Max levels must  be more than initial levels!");

    gsFileData<> fd(assemberOptionsFile);
    gsOptionList opts;
    fd.getFirst<gsOptionList>(opts);

    gsMultiPatch<> mp;
    real_t aDim;
    real_t bDim;


    real_t thickness;
    real_t Exx, Eyy, Gxy;
    real_t PoissonRatio = 0.3;
    real_t Density    = 1e0;

    if (composite)
    {
      Exx = 3300;
      Eyy = 1100;
      Gxy = 660;
      PoissonRatio = 0.25;
    }
    else
    {
      Exx  = 3102.75;
      PoissonRatio = 0.3;
    }


    if (testCase==1)
    {
      thickness = 6.35;
    }
    else if (testCase==2)
    {
      thickness = 12.7;
    }
    else if (testCase==3)
    {
      thickness = 16.75;
    }

    gsReadFile<>("surface/scordelis_lo_roof_shallow.xml", mp);

    for(index_t i = 0; i< numElevate; ++i)
      mp.patch(0).degreeElevate();    // Elevate the degree

    // h-refine
    for(index_t i = 0; i< numHref; ++i)
      mp.patch(0).uniformRefine();

    gsMultiBasis<> dbasis(mp);
    gsInfo<<"Basis (patch 0): "<< mp.patch(0).basis() << "\n";

    // Boundary conditions
    gsBoundaryConditions<> BCs;
    gsPointLoads<real_t> pLoads = gsPointLoads<real_t>();

    // Initiate Surface forces
    std::string tx("0");
    std::string ty("0");
    std::string tz("0");

    gsVector<> tmp(mp.targetDim());
    gsVector<> neu(mp.targetDim());
    tmp.setZero();
    neu.setZero();
    gsConstantFunction<> neuData(neu,mp.targetDim());

    // Unscaled load
    real_t Load = 0;

    std::string output = "solution";
    std::string dirname = "ArcLengthResults";

    gsMatrix<> writePoints(2,3);
    writePoints.col(0)<< 0.0,0.5;
    writePoints.col(1)<< 0.5,0.5;
    writePoints.col(2)<< 1.0,0.5;

    GISMO_ASSERT(mp.targetDim()==3,"Geometry must be surface (targetDim=3)!");
    // Diaphragm conditions
    BCs.addCondition(boundary::north, condition_type::dirichlet, 0, 0, false, 0 ); // unknown 0 - x
    BCs.addCondition(boundary::north, condition_type::dirichlet, 0, 0, false, 1 ); // unknown 1 - y
    BCs.addCondition(boundary::north, condition_type::dirichlet, 0, 0, false, 2 ); // unknown 2 - z
    // BCs.addCornerValue(boundary::southwest, 0.0, 0, 0); // (corner,value, patch, unknown)
    BCs.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, 0 ); // unknown 0 - x
    BCs.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, 1 ); // unknown 1 - y
    BCs.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, 2 ); // unknown 2 - z

    Load = -1e1;
    // Point loads
    gsVector<> point(2);
    gsVector<> load (3);
    point<< 0.5, 0.5 ;
    load << 0.0, 0.0, Load ;
    pLoads.addLoad(point, load, 0 );

    dirname = dirname + "/" +  "Roof_t="+ std::to_string(thickness) + "-r=" + std::to_string(numHref) + "-e" + std::to_string(numElevate) +"_solution";
    output =  "solution";
    wn = output + "data.txt";

    std::string commands = "mkdir -p " + dirname;
    const char *command = commands.c_str();
    system(command);

    // plot geometry
    if (plot)
      gsWriteParaview(mp,dirname + "/" + "mp",1000,mesh);

    if (write)
      initStepOutput(dirname + "/" + wn, writePoints);

    // Initialise solution object
    gsMultiPatch<> mp_def = mp;

    gsMaterialMatrixBase<real_t>* materialMatrix;

    real_t pi = math::atan(1)*4;
    index_t kmax = 3;

    std::vector<gsFunctionSet<> * > Gs(kmax);
    std::vector<gsFunctionSet<> * > Ts(kmax);
    std::vector<gsFunctionSet<> * > Phis(kmax);

    gsMatrix<> Gmat = gsCompositeMatrix(Exx,Eyy,Gxy,PoissonRatio,PoissonRatio*Eyy/Exx);
    Gmat.resize(Gmat.rows()*Gmat.cols(),1);
    gsConstantFunction<> Gfun(Gmat,3);
    Gs[0] = Gs[1] = Gs[2] = &Gfun;

    gsConstantFunction<> phi1,phi2,phi3;
    phi1.setValue(pi/2,3);
    phi2.setValue(0,3);
    phi3.setValue(pi/2,3);

    Phis[0] = &phi1;
    Phis[1] = &phi2;
    Phis[2] = &phi3;

    gsConstantFunction<> thicks(thickness/kmax,3);
    Ts[0] = Ts[1] = Ts[2] = &thicks;

    gsConstantFunction<> force(tmp,3);
    gsFunctionExpr<> t(std::to_string(thickness), 3);
    gsFunctionExpr<> E(std::to_string(Exx),3);
    gsFunctionExpr<> nu(std::to_string(PoissonRatio),3);
    gsFunctionExpr<> rho(std::to_string(Density),3);

    std::vector<gsFunction<>*> parameters;
    parameters.resize(2);
    parameters[0] = &E;
    parameters[1] = &nu;

    gsOptionList options;
    if (composite)
    {
        materialMatrix = new gsMaterialMatrixComposite<3,real_t>(mp,Ts,Gs,Phis);
    }
    else
    {
        options.addInt("Material","Material model: (0): SvK | (1): NH | (2): NH_ext | (3): MR | (4): Ogden",0);
        options.addInt("Implementation","Implementation: (0): Composites | (1): Analytical | (2): Generalized | (3): Spectral",1);
        materialMatrix = getMaterialMatrix<3,real_t>(mp,t,parameters,rho,options);
    }

    gsThinShellAssemblerBase<real_t>* assembler;
    assembler = new gsThinShellAssembler<3, real_t, true >(mp,dbasis,BCs,force,materialMatrix);

    // Construct assembler object
    assembler->setOptions(opts);
    assembler->setPointLoads(pLoads);

    gsStopwatch stopwatch;
    real_t time = 0.0;

    typedef std::function<gsSparseMatrix<real_t> (gsVector<real_t> const &)>                                Jacobian_t;
    typedef std::function<gsVector<real_t> (gsVector<real_t> const &, real_t, gsVector<real_t> const &) >   ALResidual_t;
    // Function for the Jacobian
    Jacobian_t Jacobian = [&time,&stopwatch,&assembler](gsVector<real_t> const &x)
    {
      gsMultiPatch<> mp_def;
      assembler->constructSolution(x,mp_def);
      assembler->assembleMatrix(mp_def);
      gsSparseMatrix<real_t> m = assembler->matrix();
      return m;
    };
    // Function for the Residual
    ALResidual_t ALResidual = [&time,&stopwatch,&assembler](gsVector<real_t> const &x, real_t lam, gsVector<real_t> const &force)
    {
      gsMultiPatch<> mp_def;
      assembler->constructSolution(x,mp_def);
      assembler->assembleVector(mp_def);
      gsVector<real_t> Fint = -(assembler->rhs() - force);
      gsVector<real_t> result = Fint - lam * force;
      return result; // - lam * force;
    };
    // Assemble linear system to obtain the force vector
    assembler->assemble();
    gsVector<> Force = assembler->rhs();

    gsArcLengthIterator<real_t> arcLength(Jacobian, ALResidual, Force);

    arcLength.options().setInt("Solver",0); // LDLT solver
    arcLength.options().setInt("BifurcationMethod",0); // 0: determinant, 1: eigenvalue
    arcLength.options().setInt("Method",method);
    arcLength.options().setReal("Length",dL);
    arcLength.options().setInt("AngleMethod",0); // 0: step, 1: iteration
    arcLength.options().setSwitch("AdaptiveLength",adaptive);
    arcLength.options().setInt("AdaptiveIterations",5);
    arcLength.options().setReal("Scaling",0.0);
    arcLength.options().setReal("Tol",tol);
    arcLength.options().setReal("TolU",tolU);
    arcLength.options().setReal("TolF",tolF);
    arcLength.options().setInt("MaxIter",maxit);
    arcLength.options().setSwitch("Verbose",true);
    arcLength.options().setReal("Relaxation",relax);
    if (quasiNewtonInt>0)
    {
      quasiNewton = true;
      arcLength.options().setInt("QuasiIterations",quasiNewtonInt);
    }
    arcLength.options().setSwitch("Quasi",quasiNewton);


    gsInfo<<arcLength.options();
    arcLength.applyOptions();
    arcLength.initialize();


    gsParaviewCollection collection(dirname + "/" + output);
    gsParaviewCollection Smembrane(dirname + "/" + "membrane");
    gsParaviewCollection Sflexural(dirname + "/" + "flexural");
    gsParaviewCollection Smembrane_p(dirname + "/" + "membrane_p");
    gsMultiPatch<> deformation = mp;

    // Make objects for previous solutions
    real_t Lguess,Lold, L0;
    gsMatrix<> Uguess,Uold, U0;
    Uold.setZero(Force.size(),1);
    U0.setZero(Force.size(),1);
    L0 = Lold = 0.0;

    gsMatrix<> solVector;
    real_t indicator = 0.0;
    arcLength.setIndicator(indicator); // RESET INDICATOR
    bool bisected = false;
    real_t dL0 = dL;
    real_t dLi = dL;      // arc-length for level i

    index_t stepi = step; // number of steps for level i

    /*
      \a solutions is a container the for each level contains the solutions per point
      \a points contains all the points across levels in the format (level, U, lambda) -------------------------------> OVERKILL? WHY NEEDED?
      \a refPoints is a container that contains (level, U, lambda) of the points from which a refinement should START in level+1
      \a errors is a container that contains the error[l][i] e_i at the ith point of level l
    */
    std::vector<std::vector<std::pair<gsVector<real_t>,real_t>>> solutions(maxLevel+1);
    solutions.reserve(maxLevel+2);
    std::vector<std::pair<index_t,std::pair<gsVector<real_t> * ,real_t * >>> points;
    std::vector<std::tuple<index_t,index_t,index_t>> refIdx; // level to compute on, level of start point, index of start point
    std::vector<std::vector<real_t>> errors(maxLevel);

    index_t level = 0;
    gsInfo<<"------------------------------------------------------------------------------------\n";
    gsInfo<<"\t\t\tLevel "<<level<<" (dL = "<<dLi<<") -- Coarse grid \n";
    gsInfo<<"------------------------------------------------------------------------------------\n";

    dLi = dL / (math::pow(2,level));
    stepi = step * (math::pow(2,level));

    std::vector<std::pair<gsVector<real_t>,real_t>> stepSolutions;
    // Add the undeformed solution
    solutions[level].push_back(std::make_pair(U0,L0));

    // Add other solutions
    for (index_t k=0; k<stepi; k++)
    {
      gsInfo<<"Load step "<< k<<"\t"<<"dL = "<<dLi<<"\n";
      // assembler->constructSolution(solVector,solution);
      arcLength.step();

      // gsInfo<<"m_U = "<<arcLength.solutionU()<<"\n";
      if (!(arcLength.converged()))
        GISMO_ERROR("Loop terminated, arc length method did not converge.\n");

      real_t lambda = arcLength.solutionL();
      solutions[level].push_back(std::make_pair(arcLength.solutionU(),lambda));
    }

    // TOLERANCE
    real_t ptol = 0.05;
    /// Start new level
    for (level = 1; level <= maxLevel; level++)
    {
      // Resize the error vector for the previous level
      errors[level-1].resize(solutions[level-1].size());

      // Add the undeformed solution
      solutions[level].push_back(std::make_pair(U0,L0));

      dLi = dL / (math::pow(2,level));
      stepi = step * (math::pow(2,level));

      gsInfo<<"------------------------------------------------------------------------------------\n";
      gsInfo<<"\t\t\tLevel "<<level<<" (dL = "<<dLi<<") -- Fine Corrector\n";
      gsInfo<<"------------------------------------------------------------------------------------\n";

      arcLength.setLength(dLi);

      for (index_t p=0; p<solutions[level-1].size()-1; p++)
      {

        std::tie(Uold,Lold) = solutions[level-1].at(p);
        gsInfo<<"Starting from (lvl,|U|,L) = ("<<level-1<<","<<Uold.norm()<<","<<Lold<<")\n";

        arcLength.setSolution(Uold,Lold);
        arcLength.resetStep();

        std::tie(Uguess,Lguess) = solutions[level-1].at(p+1);
        arcLength.setInitialGuess(Uguess,Lguess);

        for (index_t k=0; k<2; k++)
        {
          gsInfo<<"Load step "<< k<<"\t"<<"dL = "<<dLi<<"\n";
          // assembler->constructSolution(solVector,solution);
          arcLength.step();

          // gsInfo<<"m_U = "<<arcLength.solutionU()<<"\n";
          if (!(arcLength.converged()))
            GISMO_ERROR("Loop terminated, arc length method did not converge.\n");

          real_t lambda = arcLength.solutionL();
          solutions[level].push_back(std::make_pair(arcLength.solutionU(),lambda));
        }

        errors[level-1].at(p) = ( std::abs(solutions[level-1].at(p+1).second - arcLength.solutionL()) * Force.norm() + (solutions[level-1].at(p+1).first - arcLength.solutionU()).norm() ) / dLi;

        // Store as 'refinement points' the points that are on the current level and do not satisfy the error
        if (errors[level-1].at(p) > ptol)
        {
          gsInfo<<"(lvl,|U|,L) = "<<level<<","<<solutions[level-1].at(p).first.norm()<<","<<solutions[level-1].at(p).second<<") has error "<<errors[level-1].at(p)<<"\n";
          refIdx.push_back({level+1,level-1,p}); // start point of the current interval
          refIdx.push_back({level+1,level,solutions[level].size()-2}); //  mid point of the current interval
          gsInfo<<"point "<<solutions[level].size()-3<<" of level "<<level<<" added to refIdx\n";
          gsInfo<<"point "<<solutions[level].size()-2<<" of level "<<level<<" added to refIdx\n";
        }

        gsInfo<<"Finished.\n";
        // gsInfo<<"* Old solution (lvl,|U|,L) = ("<<level-1<<","<<solutions[level-1].at(p+1).second.norm()<<","<<solutions[level-1].at(p+1).first<<")\n";
        // gsInfo<<"* New solution (lvl,|U|,L) = ("<<level-1<<","
        //                                     <<arcLength.solutionU().norm()<<","<<arcLength.solutionL()<<")\n";
        // gsInfo<<"* Rel. Error   (lvl,|U|,L) = ("<<level-1<<","
        //                                     <<(solutions[level-1].at(p+1).second - arcLength.solutionU()).norm() / solutions[level-1].at(p+1).second.norm()<<","
        //                                     <<std::abs(solutions[level-1].at(p+1).first - arcLength.solutionL()) / solutions[level-1].at(p+1).first<<")\n";
        // gsInfo<<"* Rel. Error   (lvl,|U|,L) = ("<<0<<","
        //                                     <<(solutions[0].at(p+1).second - arcLength.solutionU()).norm() / solutions[0].at(p+1).second.norm()<<","
        //                                     <<std::abs(solutions[0].at(p+1).first - arcLength.solutionL()) / solutions[0].at(p+1).first<<")\n";

      }
      solutions.push_back(stepSolutions);
    }

    // Store the solutions in points
    for (index_t level =0; level<=maxLevel; ++level)
      for (index_t p = 0; p!=solutions[level].size(); ++p)
        points.push_back(std::make_pair(level,std::make_pair(&solutions[level].at(p).first,&solutions[level].at(p).second)));


    /// Refine
    gsDebugVar(refIdx.size());
    while (refIdx.size() != 0)
    {
      index_t level, reflevel, pindex;
      // Get level and index of refinement point
      std::tie(level,reflevel,pindex) = *refIdx.begin();


      gsDebugVar(level);
      gsDebugVar(reflevel);
      gsDebugVar(pindex);

      // Erase refinement index
      refIdx.erase(refIdx.begin());
      gsDebugVar(refIdx.size());

      // Check if the solutions object has already stored points at level
      if (solutions.size()-1 < level)
      {
        gsDebug<<"solutions stores "<<level+1<<" levels.";
        solutions.resize(level+1);
      }
      // Check if the errors object has already stored points at level
      gsDebugVar(errors.size());
      if (errors.size()-1 < level-1)
      {
        gsDebug<<"errors stores "<<level<<" levels.";
        errors.resize(level);
      }

      // Get starting point
      std::tie(Uold,Lold) = solutions[reflevel].at(pindex);
      gsInfo<<"Starting from (lvl,|U|,L) = ("<<reflevel<<","<<Uold.norm()<<","<<Lold<<")\n";
      arcLength.setSolution(Uold,Lold);
      arcLength.resetStep();

      solutions[level+1].push_back(std::make_pair(Uold,Lold));

      // Get initial guess
      std::tie(Uguess,Lguess) = solutions[reflevel].at(pindex+1);
      arcLength.setInitialGuess(Uguess,Lguess);

      // Set arc-length size
      dLi = dL / (math::pow(2,level));

      arcLength.setLength(dLi);
      for (index_t k=0; k<2; k++)
      {
        gsInfo<<"Load step "<< k<<"\t"<<"dL = "<<dLi<<"\n";
        // assembler->constructSolution(solVector,solution);
        arcLength.step();

        // gsInfo<<"m_U = "<<arcLength.solutionU()<<"\n";
        if (!(arcLength.converged()))
          GISMO_ERROR("Loop terminated, arc length method did not converge.\n");

        real_t lambda = arcLength.solutionL();

        solutions[level+1].push_back(std::make_pair(arcLength.solutionU(),lambda));
        points.push_back(std::make_pair(level+1,std::make_pair(&solutions[level+1].at(solutions.size()-1).first,&solutions[level+1].at(solutions.size()-1).second)));
      }

      gsDebugVar(solutions[reflevel].at(pindex+1).second);
      errors[level].at(pindex) = ( std::abs(solutions[level].at(pindex+1).second - arcLength.solutionL()) * Force.norm() + (solutions[level].at(pindex+1).first - arcLength.solutionU()).norm() ) / dLi;

      // Store as 'refinement points' the points that
      if (errors[level].at(pindex) > ptol)
      {
        // gsInfo<<"(lvl,|U|,L) = "<<level<<","<<solutions[level].at(pindex).first.norm()<<","<<solutions[level].at(pindex).second<<") has error "<<errors[level].at(pindex)<<"\n";
        // refIdx.push_back({level+1,level,pindex});
      }

    }


    #ifdef GISMO_WITH_MATPLOTLIB
        std::vector<real_t> x,y;
        std::string name;
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        plt::figure(1);
        plt::title("Solutions per level");
        for (index_t l = 0; l<=maxLevel; l++)
        {
          x.clear(); y.clear();
          x.resize(solutions[l].size()); y.resize(solutions[l].size());

          for (index_t k = 0; k!=solutions[l].size(); k++)
          {
            x[k] = solutions[l].at(k).first.norm();
            y[k] = solutions[l].at(k).second;
          }
          name = "level " + std::to_string(l);
          if (l==0){ plt::named_plot(name,x,y,"o"); }
          else { plt::named_plot(name,x,y,"o"); }
        }
        plt::xlabel("L");
        plt::ylabel("|U|");
        plt::legend();
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        plt::figure(2);
        plt::title("Solution path");
        x.clear(); y.clear();
        x.resize(points.size()); y.resize(points.size());

        for (index_t k = 0; k!=points.size(); k++)
        {
          x[k] = points.at(k).second.first->norm();
          y[k] = *(points.at(k).second.second);
        }
        name = "solution ";
        plt::named_plot(name,x,y,"o");

        x.clear(); y.clear();
        x.resize(refIdx.size()); y.resize(refIdx.size());

        index_t lvl,reflvl;
        index_t idx;
        for (index_t k = 0; k!=refIdx.size(); k++)
        {
          std::tie(reflvl,level,idx) = refIdx[k];
          x[k] = solutions[lvl].at(idx).first.norm();
          y[k] = solutions[lvl].at(idx).second;
        }
        name = "refinement points ";
        plt::named_plot(name,x,y,"o");

        plt::xlabel("L");
        plt::ylabel("|U|");
        plt::legend();
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //plt::save("./poisson2_example.png");
        plt::show();
        Py_Finalize();
    #endif















/*


// gsInfo<<"Error: Loop terminated, arc length method did not converge.\n";
// dL = dL / 2.;
// arcLength.setLength(dL);
// arcLength.setSolution(Uold,Lold);
// bisected = true;
// k -= 1;
// continue;

solVector = tuple.second;
Uold = arcLength.solutionU();
Lold = arcLength.solutionL();
assembler->constructSolution(arcLength.solutionU(),mp_def);

deformation = mp_def;
deformation.patch(0).coefs() -= mp.patch(0).coefs();// assuming 1 patch here

gsInfo<<"Total ellapsed assembly time: "<<time<<" s\n";

if (plot)
{
  gsField<> solField;
  if (deformed)
    solField= gsField<>(mp_def,deformation);
  else
    solField= gsField<>(mp,deformation);

  std::string fileName = dirname + "/" + output + util::to_string(k);
  gsWriteParaview<>(solField, fileName, 1000,mesh);
  fileName = output + util::to_string(k) + "0";
  collection.addTimestep(fileName,k,".vts");
  if (mesh) collection.addTimestep(fileName,k,"_mesh.vtp");
}
if (stress)
{
  std::string fileName;

  gsField<> membraneStress, flexuralStress, membraneStress_p;

  gsPiecewiseFunction<> membraneStresses;
  assembler->constructStress(mp_def,membraneStresses,stress_type::membrane);
  if (deformed)
    membraneStress = gsField<>(mp_def,membraneStresses,true);
  else
    membraneStress = gsField<>(mp,membraneStresses,true);

  fileName = dirname + "/" + "membrane" + util::to_string(k);
  gsWriteParaview( membraneStress, fileName, 1000);
  fileName = "membrane" + util::to_string(k) + "0";
  Smembrane.addTimestep(fileName,k,".vts");

  gsPiecewiseFunction<> flexuralStresses;
  assembler->constructStress(mp_def,flexuralStresses,stress_type::flexural);
  if (deformed)
    flexuralStress = gsField<>(mp_def,flexuralStresses, true);
  else
    flexuralStress = gsField<>(mp,flexuralStresses, true);

  fileName = dirname + "/" + "flexural" + util::to_string(k);
  gsWriteParaview( flexuralStress, fileName, 1000);
  fileName = "flexural" + util::to_string(k) + "0";
  Sflexural.addTimestep(fileName,k,".vts");
}

if (write)
  writeStepOutput(arcLength,deformation, dirname + "/" + wn, writePoints,1, 201);


*/





    if (plot)
    {
      collection.save();
      Smembrane.save();
      Sflexural.save();
      Smembrane_p.save();
    }

  return result;
}

template <class T>
gsMultiPatch<T> RectangularDomain(int n, int p, T L, T B, bool clamped, T clampoffset)
{
  gsMultiPatch<T> mp = RectangularDomain(n, n, p, p, L, B, clamped, clampoffset);
  return mp;
}

template <class T>
gsMultiPatch<T> RectangularDomain(int n, int m, int p, int q, T L, T B, bool clamped, T clampoffset)
{
  // -------------------------------------------------------------------------
  // --------------------------Make beam geometry-----------------------------
  // -------------------------------------------------------------------------
  int dim = 3; //physical dimension
  gsKnotVector<> kv0;
  kv0.initUniform(0,1,0,p+1,1);
  gsKnotVector<> kv1;
  kv1.initUniform(0,1,0,q+1,1);

  for(index_t i = 0; i< n; ++i)
      kv0.uniformRefine();
  for(index_t i = 0; i< m; ++i)
      kv1.uniformRefine();

  if (clamped)
  {
    T knotval;
    knotval = kv0.uValue(1);
    kv0.insert(std::min(clampoffset,knotval/2.));

    knotval = kv0.uValue(kv0.uSize()-2);
    kv0.insert(std::max(1-clampoffset,knotval/2.));
  }

  // Make basis
  gsTensorBSplineBasis<2,T> basis(kv0,kv1);

  // Initiate coefficient matrix
  gsMatrix<> coefs(basis.size(),dim);
  // Number of control points needed per component
  size_t len0 = basis.component(0).size();
  size_t len1 = basis.component(1).size();
  gsVector<> coefvec0(len0);
  // Uniformly distribute control points per component
  coefvec0.setLinSpaced(len0,0.0,L);
  gsVector<> coefvec1(basis.component(1).size());
  coefvec1.setLinSpaced(len1,0.0,B);

  // Z coordinate is zero
  coefs.col(2).setZero();

  // Define a matrix with ones
  gsVector<> temp(len0);
  temp.setOnes();
  for (index_t k = 0; k < len1; k++)
  {
    // First column contains x-coordinates (length)
    coefs.col(0).segment(k*len0,len0) = coefvec0;
    // Second column contains y-coordinates (width)
    coefs.col(1).segment(k*len0,len0) = temp*coefvec1.at(k);
  }
  // Create gsGeometry-derived object for the patch
  gsTensorBSpline<2,real_t> shape(basis,coefs);

  gsMultiPatch<T> mp;
  mp.addPatch(shape);
  mp.addAutoBoundaries();

  return mp;
}

template <class T>
void addClamping(gsMultiPatch<T>& mp, index_t patch, std::vector<boxSide> sides, T offset) //, std::vector<boxSide> sides, T offset)
{

    gsTensorBSpline<2,T> *geo = dynamic_cast< gsTensorBSpline<2,real_t> * > (&mp.patch(patch));

    T dknot0 = geo->basis().component(0).knots().minIntervalLength();
    T dknot1 = geo->basis().component(1).knots().minIntervalLength();

    gsInfo<<"sides.size() = "<<sides.size()<<"\n";

    index_t k =0;


    for (std::vector<boxSide>::iterator it = sides.begin(); it != sides.end(); it++)
    {
        gsInfo<<"side = "<<(*it)<<"\n";

      if (*it==boundary::west || *it==boundary::east) // west or east
      {
        if (*it==boundary::east) // east, val = 1
          geo->insertKnot(1 - std::min(offset, dknot0 / 2),0);
        else if (*it==boundary::west) // west
          geo->insertKnot(std::min(offset, dknot0 / 2),0);
      }
      else if (*it==boundary::south || *it==boundary::north) // west or east
      {
       if (*it==boundary::north) // north
         geo->insertKnot(1 - std::min(offset, dknot0 / 2),1);
       else if (*it==boundary::south) // south
         geo->insertKnot(std::min(offset, dknot0 / 2),1);
      }
      else if (*it==boundary::none)
        gsWarn<<*it<<"\n";
      else
        GISMO_ERROR("Side unknown, side = " <<*it);

        k++;
gsInfo<<"k = "<<k<<"\n";
    }
}

template <class T>
gsMultiPatch<T> Rectangle(T L, T B) //, int n, int m, std::vector<boxSide> sides, T offset)
{
  // -------------------------------------------------------------------------
  // --------------------------Make beam geometry-----------------------------
  // -------------------------------------------------------------------------
  int dim = 3; //physical dimension
  gsKnotVector<> kv0;
  kv0.initUniform(0,1,0,2,1);
  gsKnotVector<> kv1;
  kv1.initUniform(0,1,0,2,1);

  // Make basis
  gsTensorBSplineBasis<2,T> basis(kv0,kv1);

  // Initiate coefficient matrix
  gsMatrix<> coefs(basis.size(),dim);
  // Number of control points needed per component
  size_t len0 = basis.component(0).size();
  size_t len1 = basis.component(1).size();
  gsVector<> coefvec0(len0);
  // Uniformly distribute control points per component
  coefvec0.setLinSpaced(len0,0.0,L);
  gsVector<> coefvec1(basis.component(1).size());
  coefvec1.setLinSpaced(len1,0.0,B);

  // Z coordinate is zero
  coefs.col(2).setZero();

  // Define a matrix with ones
  gsVector<> temp(len0);
  temp.setOnes();
  for (size_t k = 0; k < len1; k++)
  {
    // First column contains x-coordinates (length)
    coefs.col(0).segment(k*len0,len0) = coefvec0;
    // Second column contains y-coordinates (width)
    coefs.col(1).segment(k*len0,len0) = temp*coefvec1.at(k);
  }
  // Create gsGeometry-derived object for the patch
  gsTensorBSpline<2,real_t> shape(basis,coefs);

  gsMultiPatch<T> mp;
  mp.addPatch(shape);
  mp.addAutoBoundaries();

  return mp;
}


template <class T>
gsMultiPatch<T> AnnularDomain(int n, int p, T R1, T R2)
{
  // -------------------------------------------------------------------------
  // --------------------------Make beam geometry-----------------------------
  // -------------------------------------------------------------------------
  int dim = 3; //physical dimension
  gsKnotVector<> kv0;
  kv0.initUniform(0,1,0,3,1);
  gsKnotVector<> kv1;
  kv1.initUniform(0,1,0,3,1);

  // Make basis
  // gsTensorNurbsBasis<2,T> basis(kv0,kv1);

  // Initiate coefficient matrix
  gsMatrix<> coefs(9,dim);

  coefs<<R1,0,0,
  (R1+R2)/2,0,0,
  R2,0,0,
  R1,R1,0,
  (R1+R2)/2,(R1+R2)/2,0,
  R2,R2,0,
  0,R1,0,
  0,(R1+R2)/2,0,
  0,R2,0;

  gsMatrix<> weights(9,1);
  weights<<1,1,1,
  0.707106781186548,0.707106781186548,0.707106781186548,
  1,1,1;

  // Create gsGeometry-derived object for the patch
  gsTensorNurbs<2,real_t> shape(kv0,kv1,coefs,weights);


  gsMultiPatch<T> mp;
  mp.addPatch(shape);
  mp.addAutoBoundaries();

  // Elevate up to order p
  if (p>2)
  {
    for(index_t i = 2; i< p; ++i)
        mp.patch(0).degreeElevate();    // Elevate the degree
  }

  // Refine n times
  for(index_t i = 0; i< n; ++i)
      mp.patch(0).uniformRefine();

  return mp;
}

template <class T>
gsMultiPatch<T> FrustrumDomain(int n, int p, T R1, T R2, T h)
{
  // -------------------------------------------------------------------------
  // --------------------------Make beam geometry-----------------------------
  // -------------------------------------------------------------------------
  // n = number of uniform refinements over the height; n = 0, only top and bottom part

  int dim = 3; //physical dimension
  gsKnotVector<> kv0;
  kv0.initUniform(0,1,0,3,1);
  gsKnotVector<> kv1;
  kv1.initUniform(0,1,0,3,1);

  // Refine n times
  for(index_t i = 0; i< n; ++i)
      kv1.uniformRefine();

  gsDebug<<kv1;

  // Make basis
  // gsTensorNurbsBasis<2,T> basis(kv0,kv1);

  // Initiate coefficient matrix
  index_t N = math::pow(2,n)+2;
  gsMatrix<> coefs(3*N,dim);
  gsMatrix<> tmp(3,3);
  T R,H;

  gsMatrix<> weights(3*N,1);
  for (index_t k=0; k!= N; k++)
  {
    R = k*(R2-R1)/(N-1) + R1;
    H = k*h/(N-1);
    tmp<< R,0,H,
          R,R,H,
          0,R,H;

    coefs.block(3*k,0,3,3) = tmp;

    weights.block(3*k,0,3,1) << 1,0.70711,1;
  }

  // Create gsGeometry-derived object for the patch
  gsTensorNurbs<2,real_t> shape(kv0,kv1,coefs,weights);

  gsMultiPatch<T> mp;
  mp.addPatch(shape);
  mp.addAutoBoundaries();

  // Elevate up to order p
  if (p>2)
  {
    for(index_t i = 2; i< p; ++i)
        mp.patch(0).degreeElevate();    // Elevate the degree
  }

  // // Refine n times
  // for(index_t i = 0; i< n; ++i)
  //     mp.patch(0).uniformRefine();

  return mp;
}

template <class T>
void initStepOutput(const std::string name, const gsMatrix<T> & points)
{
  std::ofstream file;
  file.open(name,std::ofstream::out);
  file  << std::setprecision(20)
        << "Deformation norm" << ",";
        for (index_t k=0; k!=points.cols(); k++)
        {
          file<< "point "<<k<<" - x" << ","
              << "point "<<k<<" - y" << ","
              << "point "<<k<<" - z" << ",";
        }

  file  << "Lambda" << ","
        << "Indicator"
        << "\n";
  file.close();

  gsInfo<<"Step results will be written in file: "<<name<<"\n";
}

template <class T>
void writeStepOutput(const gsArcLengthIterator<T> & arcLength, const gsMultiPatch<T> & deformation, const std::string name, const gsMatrix<T> & points, const index_t extreme, const index_t kmax) // extreme: the column of point indices to compute the extreme over (default -1)
{
  gsMatrix<T> P(2,1), Q(2,1);
  gsMatrix<T> out(3,points.cols());
  gsMatrix<T> tmp;

  for (index_t p=0; p!=points.cols(); p++)
  {
    P<<points.col(p);
    deformation.patch(0).eval_into(P,tmp);
    out.col(p) = tmp;
  }

  std::ofstream file;
  file.open(name,std::ofstream::out | std::ofstream::app);
  if (extreme==-1)
  {
    file  << std::setprecision(6)
          << arcLength.solutionU().norm() << ",";
          for (index_t p=0; p!=points.cols(); p++)
          {
            file<< out(0,p) << ","
                << out(1,p) << ","
                << out(2,p) << ",";
          }

    file  << arcLength.solutionL() << ","
          << arcLength.indicator() << ","
          << "\n";
  }
  else if (extreme==0 || extreme==1)
  {
    gsMatrix<T> out2(kmax,points.cols()); // evaluation points in the rows, output (per coordinate) in columns
    for (int p = 0; p != points.cols(); p ++)
    {
      Q.at(1-extreme) = points(1-extreme,p);
      for (int k = 0; k != kmax; k ++)
      {
        Q.at(extreme) = 1.0*k/(kmax-1);
        deformation.patch(0).eval_into(Q,tmp);
        out2(k,p) = tmp.at(2); // z coordinate
      }
    }

    file  << std::setprecision(6)
          << arcLength.solutionU().norm() << ",";
          for (index_t p=0; p!=points.cols(); p++)
          {
            file<< out(0,p) << ","
                << out(1,p) << ","
                << std::max(abs(out2.col(p).maxCoeff()),abs(out2.col(p).minCoeff())) << ",";
          }

    file  << arcLength.solutionL() << ","
          << arcLength.indicator() << ","
          << "\n";
  }
  else
    GISMO_ERROR("Extremes setting unknown");

  file.close();
}
