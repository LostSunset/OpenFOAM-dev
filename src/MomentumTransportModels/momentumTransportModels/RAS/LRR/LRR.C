/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2011-2024 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "LRR.H"
#include "fvModels.H"
#include "fvConstraints.H"
#include "wallDist.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace RASModels
{

// * * * * * * * * * * * * Protected Member Functions  * * * * * * * * * * * //

template<class BasicMomentumTransportModel>
tmp<volScalarField> LRR<BasicMomentumTransportModel>::boundEpsilon()
{
    tmp<volScalarField> tCmuk2(Cmu_*sqr(k_));
    epsilon_ = max(epsilon_, tCmuk2()/(this->nutMaxCoeff_*this->nu()));
    return tCmuk2;
}


template<class BasicMomentumTransportModel>
void LRR<BasicMomentumTransportModel>::correctNut()
{
    this->nut_ = boundEpsilon()/epsilon_;
    this->nut_.correctBoundaryConditions();
    fvConstraints::New(this->mesh_).constrain(this->nut_);
}


template<class BasicMomentumTransportModel>
tmp<fvScalarMatrix> LRR<BasicMomentumTransportModel>::epsilonSource() const
{
    return tmp<fvScalarMatrix>
    (
        new fvScalarMatrix
        (
            epsilon_,
            dimVolume*this->rho_.dimensions()*epsilon_.dimensions()/dimTime
        )
    );
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

template<class BasicMomentumTransportModel>
LRR<BasicMomentumTransportModel>::LRR
(
    const alphaField& alpha,
    const rhoField& rho,
    const volVectorField& U,
    const surfaceScalarField& alphaRhoPhi,
    const surfaceScalarField& phi,
    const viscosity& viscosity,
    const word& type
)
:
    ReynoldsStress<RASModel<BasicMomentumTransportModel>>
    (
        type,
        alpha,
        rho,
        U,
        alphaRhoPhi,
        phi,
        viscosity
    ),

    Cmu_("Cmu", this->coeffDict(), 0.09),
    C1_("C1", this->coeffDict(), 1.8),
    C2_("C2", this->coeffDict(), 0.6),
    Ceps1_("Ceps1", this->coeffDict(), 1.44),
    Ceps2_("Ceps2", this->coeffDict(), 1.92),
    Cs_("Cs", this->coeffDict(), 0.25),
    Ceps_("Ceps", this->coeffDict(), 0.15),

    wallReflection_
    (
        this->coeffDict().template lookupOrDefault<Switch>
        (
            "wallReflection",
            true
        )
    ),
    kappa_("kappa", this->coeffDict(), 0.41),
    Cref1_("Cref1", this->coeffDict(), 0.5),
    Cref2_("Cref2", this->coeffDict(), 0.3),

    k_
    (
        IOobject
        (
            this->groupName("k"),
            this->runTime_.name(),
            this->mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        0.5*tr(this->R_)
    ),
    epsilon_
    (
        IOobject
        (
            this->groupName("epsilon"),
            this->runTime_.name(),
            this->mesh_,
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        ),
        this->mesh_
    )
{
    if (type == typeName)
    {
        this->boundNormalStress(this->R_);
        boundEpsilon();
        k_ = 0.5*tr(this->R_);
    }
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

template<class BasicMomentumTransportModel>
bool LRR<BasicMomentumTransportModel>::read()
{
    if (ReynoldsStress<RASModel<BasicMomentumTransportModel>>::read())
    {
        Cmu_.readIfPresent(this->coeffDict());
        C1_.readIfPresent(this->coeffDict());
        C2_.readIfPresent(this->coeffDict());
        Ceps1_.readIfPresent(this->coeffDict());
        Ceps2_.readIfPresent(this->coeffDict());
        Cs_.readIfPresent(this->coeffDict());
        Ceps_.readIfPresent(this->coeffDict());

        wallReflection_.readIfPresent("wallReflection", this->coeffDict());
        kappa_.readIfPresent(this->coeffDict());
        Cref1_.readIfPresent(this->coeffDict());
        Cref2_.readIfPresent(this->coeffDict());

        return true;
    }
    else
    {
        return false;
    }
}


template<class BasicMomentumTransportModel>
tmp<volSymmTensorField> LRR<BasicMomentumTransportModel>::DREff() const
{
    return volSymmTensorField::New
    (
        "DREff",
        (Cs_*(this->k_/this->epsilon_))*this->R_ + I*this->nu()
    );
}


template<class BasicMomentumTransportModel>
tmp<volSymmTensorField> LRR<BasicMomentumTransportModel>::DepsilonEff() const
{
    return volSymmTensorField::New
    (
        "DepsilonEff",
        (Ceps_*(this->k_/this->epsilon_))*this->R_ + I*this->nu()
    );
}


template<class BasicMomentumTransportModel>
void LRR<BasicMomentumTransportModel>::correct()
{
    if (!this->turbulence_)
    {
        return;
    }

    // Local references
    const alphaField& alpha = this->alpha_;
    const rhoField& rho = this->rho_;
    const surfaceScalarField& alphaRhoPhi = this->alphaRhoPhi_;
    const volVectorField& U = this->U_;
    volSymmTensorField& R = this->R_;
    const Foam::fvModels& fvModels(Foam::fvModels::New(this->mesh_));
    const Foam::fvConstraints& fvConstraints
    (
        Foam::fvConstraints::New(this->mesh_)
    );

    ReynoldsStress<RASModel<BasicMomentumTransportModel>>::correct();

    tmp<volTensorField> tgradU(fvc::grad(U));
    const volTensorField& gradU = tgradU();

    volSymmTensorField P(-twoSymm(R & gradU));
    volScalarField G(this->GName(), 0.5*mag(tr(P)));

    // Update epsilon and G at the wall
    epsilon_.boundaryFieldRef().updateCoeffs();

    // Dissipation equation
    tmp<fvScalarMatrix> epsEqn
    (
        fvm::ddt(alpha, rho, epsilon_)
      + fvm::div(alphaRhoPhi, epsilon_)
      - fvm::laplacian(alpha*rho*DepsilonEff(), epsilon_)
     ==
        Ceps1_*alpha*rho*G*epsilon_/k_
      - fvm::Sp(Ceps2_*alpha*rho*epsilon_/k_, epsilon_)
      + epsilonSource()
      + fvModels.source(alpha, rho, epsilon_)
    );

    epsEqn.ref().relax();
    fvConstraints.constrain(epsEqn.ref());
    epsEqn.ref().boundaryManipulate(epsilon_.boundaryFieldRef());
    solve(epsEqn);
    fvConstraints.constrain(epsilon_);
    boundEpsilon();


    // Correct the trace of the tensorial production to be consistent
    // with the near-wall generation from the wall-functions
    const fvPatchList& patches = this->mesh_.boundary();

    forAll(patches, patchi)
    {
        const fvPatch& curPatch = patches[patchi];

        if (isA<wallFvPatch>(curPatch))
        {
            forAll(curPatch, facei)
            {
                label celli = curPatch.faceCells()[facei];
                P[celli] *= min
                (
                    G[celli]/(0.5*mag(tr(P[celli])) + small),
                    1.0
                );
            }
        }
    }

    // Reynolds stress equation
    tmp<fvSymmTensorMatrix> REqn
    (
        fvm::ddt(alpha, rho, R)
      + fvm::div(alphaRhoPhi, R)
      - fvm::laplacian(alpha*rho*DREff(), R)
      + fvm::Sp(C1_*alpha*rho*epsilon_/k_, R)
      ==
        alpha*rho*P
      - (2.0/3.0*(1 - C1_)*I)*alpha*rho*epsilon_
      - C2_*alpha*rho*dev(P)
      + this->RSource()
      + fvModels.source(alpha, rho, R)
    );

    // Optionally add wall-refection term
    if (wallReflection_)
    {
        const volVectorField& n(wallDist::New(this->mesh_).n());
        const volScalarField& y(wallDist::New(this->mesh_).y());

        const volSymmTensorField reflect
        (
            Cref1_*R - ((Cref2_*C2_)*(k_/epsilon_))*dev(P)
        );

        REqn.ref() +=
            ((3*pow(Cmu_, 0.75)/kappa_)*(alpha*rho*sqrt(k_)/y))
           *dev(symm((n & reflect)*n));
    }

    REqn.ref().relax();
    fvConstraints.constrain(REqn.ref());
    solve(REqn);
    fvConstraints.constrain(R);

    this->boundNormalStress(R);

    k_ = 0.5*tr(R);

    correctNut();

    // Correct wall shear-stresses when applying wall-functions
    this->correctWallShearStress(R);
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace RASModels
} // End namespace Foam

// ************************************************************************* //
