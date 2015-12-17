/** @file gsAssemblerOptions.h

    @brief Provides assembler and solver options.

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): A. Mantzaflaris
*/

#pragma once

namespace gismo
{

struct dirichlet
{	
	enum strategy
	{
        elimination  = 11, ///< Enforce Dirichlet BCs by eliminating them from the system

        penalize     = 13, ///< Penalize the diagonal at the position of Dirichlet DoFs,

        nitsche      = 12, ///< Enforce the boundary condition weakly by a penalty term
        
        /// Compute Dirichlet DoFs in the normal direction (for a vector valued function),
        /// The tangential component are handled with the Nitsche method.
        eliminatNormal = 14,

        none         = 0 ///<< Do absolutely nothing for Dirichlet boundary conditions.
	};

	enum values
	{
        homogeneous   = 100, ///< Assume homogeneous Dirichlet conditions

        interpolation = 101, ///< Compute Dirichlet DoFs by using interpolation on the boundary
        
        l2Projection  = 102, ///< Compute Dirichlet DoFs by using L2 projection on the boundary
        
        user          = 103 ///< User will provide values of the Dirichlet dofs
    };
};

struct iFace
{	
	enum strategy
	{
        /// Glue patches together by merging DoFs across an
        /// interface into one. This only works for conforming
	    /// interfaces.
	    conforming = 1,
        glue       = 1,
	    
	    /// Use discontinuous Galerkin-like coupling between
	    /// adjacent patches.
	    dg = 2,

	    /// Use enhanced smoothness splines between interfaces of adjacent patches.
	    smooth = 3,
        
	    /// Do absolutely nothing for coupling the interfaces.
	    none = 0
	};

};

/*
	enum iFaceTopology
	{
        nested   = 1,

        clamped  = 2, 
    }


struct transform
{	
	enum type
	{
	    Hgrad = 1, // covariant, inverse_composition
	    Hdiv  = 2, // Piola 
	    Hcurl = 3
	};
};

// for mixed formulations
struct space
{	
	enum type
	{
	    taylorHood    = 1,
	    raviartThomas = 2,

	    none          = 0
	};
};
*/

struct gsAssemblerOptions
{
public:
    // Default constructor
    gsAssemblerOptions()
    : dirValues    (dirichlet::l2Projection ), 
      dirStrategy  (dirichlet::elimination  ), 
      intStrategy  (iFace    ::conforming   ),
      // transformType(transform::Hgrad        ),
      // spaceType    (space    ::taylorHood   )
      bdA(2.0),
      bdB(1  ),
      memOverhead(0.33334),
      quA(1.0),
      quB(1  )
    { }

public:

    dirichlet::values    dirValues;

    dirichlet::strategy  dirStrategy;

    iFace::strategy      intStrategy;

    // transform::type      transformType;
    // space::type          spaceType;

    // If set to a value different than zero, it controls the
    // allocation of the sparse matrix, ie. the maximum number of
    // non-zero entries per column (set to: A * p + B)
    double bdA;
    int    bdB;

    // more memory is allocated then required for efficency reasons,
    // more precise, (1+memOverhead) times the original memory is allocated
    // default value is 0.33334 -> 75% of the allocated memory is used.
    double memOverhead;

    // The formula for the number of quadrature points for all
    // integral computations will be set to the integer which is
    // closest to (A * p + B), where \a p is the (coordinate-wise)
    // degree of the basis
    double quA;
    int    quB;

public: /* Utility functions that return values implied by the settings*/


    index_t numQuNodes(const gsBasis<real_t> & b) const
    {
        index_t res = 1;
        for( int i=0; i<b.domainDim(); ++i )
        {
            res *= static_cast<index_t>(quA * b.degree(i) + quB + 0.5);
        }

        return res;
    }

    index_t numColNz(const gsBasis<real_t> & b) const
    {
        index_t nz = 1;
        for (int i = 0; i != b.dim(); ++i)
            nz *= static_cast<index_t>(bdA * b.degree(i) + bdB + 0.5);
        return static_cast<index_t>(nz*(1.0+memOverhead));
    }

};

} // namespace gismo
