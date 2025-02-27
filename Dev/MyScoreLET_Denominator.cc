// Scorer for LET_Denominator
//
// ********************************************************************
// *                                                                  *
// *                                                                  *
// * This file was obtained from Topas MC Inc under the license       *
// * agreement set forth at http://www.topasmc.org/registration       *
// * Any use of this file constitutes full acceptance of              *
// * this TOPAS MC license agreement.                                 *
// *                                                                  *
// ********************************************************************
//

#include "MyScoreLET_Denominator.hh"

#include "TsParameterManager.hh"

#include "G4ParticleTable.hh"
#include "G4ParticleDefinition.hh"
#include "G4SystemOfUnits.hh"

MyScoreLET_Denominator::MyScoreLET_Denominator(TsParameterManager* pM, TsMaterialManager* mM, TsGeometryManager* gM, TsScoringManager* scM, TsExtensionManager* eM,
														   G4String scorerName, G4String quantity, G4String outFileName, G4bool isSubScorer)
	:TsVBinnedScorer(pM, mM, gM, scM, eM, scorerName, quantity, outFileName, isSubScorer)
{
	SetUnit("");

	fProtonDefinition = G4ParticleTable::GetParticleTable()->FindParticle("proton");
	fElectronDefinition = G4ParticleTable::GetParticleTable()->FindParticle("e-");

	fStepCount = 0;

	// Dose-averaged or fluence-averaged LET definition
	G4String weightType = "dose";
	if (fPm->ParameterExists(GetFullParmName("WeightBy")))
		weightType = fPm->GetStringParameter(GetFullParmName("WeightBy"));
	weightType.toLower();

	if (weightType == "dose") {
		fDoseWeighted = true;
	} else if (weightType == "fluence" || weightType == "track") {
		fDoseWeighted = false;
	} else {
		G4cerr << "Topas is exiting due to a serious error in scoring setup." << G4endl;
		G4cerr << GetFullParmName("WeightBy") << " refers to an unknown weighting: " << weightType << G4endl;
		exit(1);
	}

	includeAll = false;
	// Check which particles to include in TOPAS
	G4String* includeparticles;
	if (fPm->ParameterExists(GetFullParmName("includeparticles"))){
		includeparticles = fPm->GetStringVector(GetFullParmName("includeparticles"));
		G4int length = fPm->GetVectorLength(GetFullParmName("includeparticles"));
		for (G4int i = 0; i < length; i++) {
			G4ParticleDefinition* pdef = G4ParticleTable::GetParticleTable()->FindParticle(includeparticles[i]);
			p.push_back(pdef);
		}

	}
	else {
		G4cout << "All particles are included in LET scoring";
		includeAll = true;
	}

	// Upper cutoff to dE/dx (used to fix spikes). Neglected if zero/negative.
	fMaxScoredLET = 0;
	if (fPm->ParameterExists(GetFullParmName("MaxScoredLET")))
		fMaxScoredLET = fPm->GetDoubleParameter(GetFullParmName("MaxScoredLET"), "force per density");

	// Neglect secondary electrons in low density materials where the mean path length between discrete processes is
	// longer than the voxel width. Otherwise the LET is too high.
	fNeglectSecondariesBelowDensity = 0;
	if (fPm->ParameterExists(GetFullParmName("NeglectSecondariesBelowDensity")))
		fNeglectSecondariesBelowDensity = fPm->GetDoubleParameter(GetFullParmName("NeglectSecondariesBelowDensity"), "Volumic Mass");

	// Use fluence-weighted LET for low density materials in order to avoid rare spikes (a.k.a. pretty plot mode)
	// Disabled by default
	fUseFluenceWeightedBelowDensity = 0;
	if (fPm->ParameterExists(GetFullParmName("UseFluenceWeightedBelowDensity")))
		fUseFluenceWeightedBelowDensity = fPm->GetDoubleParameter(GetFullParmName("UseFluenceWeightedBelowDensity"), "Volumic Mass");
}


MyScoreLET_Denominator::~MyScoreLET_Denominator()
{;}


G4bool MyScoreLET_Denominator::ProcessHits(G4Step* aStep,G4TouchableHistory*)
{
	if (!fIsActive) {
		fSkippedWhileInactive++;
		return false;
	}
	// Checks if particle in scoring volume is part of the lists of desired particles to score LET
	const G4ParticleDefinition* PDef = aStep->GetTrack()->GetParticleDefinition();
	if (includeAll == false){
		if (std::find(p.begin(), p.end(), PDef) != p.end()){
		}
		else {
			return false;
		}

	}

		G4double stepLength = aStep->GetStepLength();
		if (stepLength <= 0.)
			return false;

		G4double eDep = aStep->GetTotalEnergyDeposit();
		G4double density = aStep->GetPreStepPoint()->GetMaterial()->GetDensity();

		// If step is fluence-weighted, we can avoid a lot of logic
		G4bool isStepFluenceWeighted = !fDoseWeighted || density < fUseFluenceWeightedBelowDensity;
		if (isStepFluenceWeighted) {
			AccumulateHit(aStep, stepLength/mm);
			return true;
		}
		// otherwise we just have to calculate everything again



		// Add energy deposited by secondary electrons (unless NeglectSecondariesBelowDensity is used)
		if (density > fNeglectSecondariesBelowDensity) {
			const G4TrackVector* secondary = aStep->GetSecondary();
			if (!secondary) {
				secondary = aStep->GetTrack()->GetStep()->GetSecondary();  // parallel worlds
			}
			if (secondary) {
				G4int diff;
				if (fStepCount == 0) diff = 0;
				else diff = secondary->size() - fStepCount;

				fStepCount = (*secondary).size();

				for (unsigned int i=(*secondary).size()-diff; i<(*secondary).size(); i++)
					if ((*secondary)[i]->GetParticleDefinition() == fElectronDefinition)
						eDep += (*secondary)[i]->GetKineticEnergy();
			}
		}

		// Compute LET
		G4double dEdx = eDep / stepLength;

		// If dose-weighted and not using PreStepLookup, only score LET if below MaxScoredLET
		if (fMaxScoredLET <= 0 || dEdx / density < fMaxScoredLET) {
			AccumulateHit(aStep, eDep/MeV);
			return true;
		}

	return false;
}
