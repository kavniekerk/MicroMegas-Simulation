#include <sstream>
#include <iostream>
#include <iomanip>

#include <TCanvas.h>
#include <TApplication.h>
#include <TFile.h>
#include <TTree.h>
#include <TRandom3.h>
#include <TVector3.h>
#include <TMath.h>

#include "MediumMagboltz.hh"
#include "ComponentElmer.hh"
#include "ComponentVoxel.hh"
#include "Sensor.hh"
#include "ViewField.hh"
#include "ViewCell.hh"
#include "Plotting.hh"
#include "ViewFEMesh.hh"
#include "ViewSignal.hh"
#include "GarfieldConstants.hh"
//#include "Random.hh"
#include "AvalancheMicroscopic.hh"

using namespace std;
using namespace Garfield;

int main(int argc, char * argv[]) {
	/* [[[cog
	from MMconfig import *
	cog.outl(
		"""
		const int maxAvalancheSize = {}; // constrains the maximum avalanche size, 0 means no limit
		double areaXmin = {}, areaXmax = -areaXmin;
		double areaYmin = {}, areaYmax = -areaYmin;
		double areaZmin = {}, areaZmax = {} + {}; // begin and end of the drift region, 100µm above the mesh where the field gets inhomogeneous (value from: http://iopscience.iop.org/article/10.1088/1748-0221/6/06/P06011/pdf)
		""".format(
			conf["amplification"]["max_avalanche_size"],
			-float(conf["detector"]["size_x"])/2.,
			-float(conf["detector"]["size_y"])/2.,
			conf["amplification"]["z_min"], conf["amplification"]["z_max"], conf["amplification"]["z_max_safety"]
		)
	)
	]]] */

	const int maxAvalancheSize = 0; // constrains the maximum avalanche size, 0 means no limit
	double areaXmin = -4.5, areaXmax = -areaXmin;
	double areaYmin = -4.5, areaYmax = -areaYmin;
	double areaZmin = -20e-4, areaZmax = 100.e-4 + 200e-4; // begin and end of the drift region, 100µm above the mesh where the field gets inhomogeneous (value from: http://iopscience.iop.org/article/10.1088/1748-0221/6/06/P06011/pdf)

	// [[[end]]]

	TString inputfileName, outputfileName;
	if (argc == 3) {
		inputfileName = argv[1];
		outputfileName = argv[2];
	} else if (argc == 2) {
		cerr << "Only input or output file specified, give both!" << endl;
	} else {
		// use file from conf
		/*[[[cog
		from MMconfig import *
		cog.outl("inputfileName = \"{}\";".format(conf["amplification"]["in_filename"]))
		cog.outl("outputfileName = \"{}\";".format(conf["amplification"]["out_filename"]))
		]]]*/
		inputfileName = "/localscratch/simulation_files/MicroMegas-Simulation/outfiles/drift.root";
		outputfileName = "/localscratch/simulation_files/MicroMegas-Simulation/outfiles/avalanche.root";
		//[[[end]]]
	}

	if (!inputfileName || !outputfileName) {
		cerr << "No input/output file specified or given!" << endl;
		return 1;
	}

	TFile* inputFile = TFile::Open(inputfileName);
	if (!inputFile->IsOpen()) {
		cout << "Error opening file: " << argv[1] << endl;
		return 1;
	}
	TTree* inputTree = (TTree*)inputFile->Get("driftTree");
	Int_t numberOfEvents = inputTree->GetEntriesFast();

	Int_t inNele;
	vector<Double_t> *inPosX = 0, *inPosY = 0, *inPosZ = 0, *inEkin = 0, *inT = 0;
	inputTree->SetBranchAddress("x1", &inPosX); inputTree->SetBranchAddress("y1", &inPosY);	inputTree->SetBranchAddress("z1", &inPosZ);
	inputTree->SetBranchAddress("e1", &inEkin);
	inputTree->SetBranchAddress("t1", &inT);
	inputTree->SetBranchAddress("nele", &inNele);
	cout << "Reading " << numberOfEvents << " events from " << inputFile->GetPath() << endl;

	Int_t nele;  // number of electrons in avalanche
	Int_t nelep; // number of electron end points
	vector<Int_t> status;
	vector<Double_t> x0, y0, z0, e0, t0;
	vector<Double_t> x1, y1, z1, e1, t1;

	TFile* outputFile = new TFile(outputfileName, "RECREATE");
	outputFile->cd();
	TTree* outputTree = new TTree("avalancheTree", "Avalanches");
	outputTree->Branch("nele", &nele, "nele/I");
	outputTree->Branch("nelep", &nelep, "nelep/I");
	outputTree->Branch("status", &status);
	outputTree->Branch("x0", &x0); outputTree->Branch("y0", &y0); outputTree->Branch("z0", &z0); outputTree->Branch("e0", &e0); outputTree->Branch("t0", &t0);
	outputTree->Branch("x1", &x1); outputTree->Branch("y1", &y1); outputTree->Branch("z1", &z1); outputTree->Branch("e1", &e1); outputTree->Branch("t1", &t1);

	ComponentVoxel *fm = new ComponentVoxel();
    fm->SetMesh(10,10,40, -64e-4,64e-4, -64e-4,64e-4, -154e-6,300e-6);
    fm->LoadData("field.txt", "XYZ", true, false, 1e-4, 1., 1.);
    fm->EnablePeriodicityX();
    fm->EnablePeriodicityY();

	// Define the medium
	MediumMagboltz* gas = new MediumMagboltz();
	/*[[[cog
	from MMconfig import *
	gas_composition = eval(conf["detector"]["gas_composition"])
	cog.outl("gas->SetComposition({});".format(', '.join(['\"{}\",{}'.format(comp, fract) for comp, fract in gas_composition.items()])))
	cog.outl("gas->SetTemperature({}+273.15);".format(conf["detector"]["temperature"]))
	cog.outl("gas->SetPressure({} * 7.50062);".format(conf["detector"]["pressure"]))
	]]]*/
	gas->SetComposition("ar",93.0, "co2",7.0);
	gas->SetTemperature(20.+273.15);
	gas->SetPressure(100. * 7.50062);
	//[[[end]]]
	gas->EnableDrift();							// Allow for drifting in this medium
	gas->SetMaxElectronEnergy(200.);
	gas->Initialise(true);
	fm->SetMedium(0, gas);

	Sensor* sensor = new Sensor();
	sensor->AddComponent(fm);
	sensor->SetArea(areaXmin, areaYmin, areaZmin, areaXmax, areaYmax, areaZmax);
	sensor->AddElectrode(fm, "readout");
	sensor->SetTimeWindow(-2., 0.1, 80);

	AvalancheMicroscopic* avalanchemicroscopic = new AvalancheMicroscopic();
	avalanchemicroscopic->SetSensor(sensor);
	avalanchemicroscopic->SetCollisionSteps(1);
	if (maxAvalancheSize > 0) avalanchemicroscopic->EnableAvalancheSizeLimit(maxAvalancheSize);
	//avalanchemicroscopic->EnableSignalCalculation();

	/*
	TApplication app("app", &argc, argv);
	ViewDrift* viewdrift = new ViewDrift();
	viewdrift->SetArea(areaXmin, areaYmin, areaZmin-0.001, areaXmax, areaYmax, areaZmax+0.001);
	avalanchemicroscopic->EnablePlotting(viewdrift);
	*/

	// actual simulation
	for (int i=0; i<numberOfEvents; i++) {
		int numberOfElectrons;

		inputTree->GetEvent(i, 0); // 0 get only active branches, 1 get all branches
		//inputTree->Show(i);
		numberOfElectrons = inNele;

		for (int e=0; e<numberOfElectrons; e++) {
			// Set the initial position [cm], direction, starting time [ns] and initial energy [eV]
			//[[[cog from MMconfig import *; cog.outl("TVector3 initialPosition = TVector3(inPosX->at(e), inPosY->at(e), {});".format(conf["amplification"]["z_max"])) ]]]
			TVector3 initialPosition = TVector3(inPosX->at(e), inPosY->at(e), 100.e-4);
			//[[[end]]]
			//TVector3 initialPosition = TVector3(inPosX->at(e), inPosY->at(e), inPosZ->at(e));
			TVector3 initialDirection = TVector3(0., 0., -1.); // 0,0,0 for random initial direction
			Double_t initialTime = inT->at(e);
			Double_t initialEnergy = inEkin->at(e); // override default energy

			//cout << "Initial Time    : " << initialTime << " ns" << endl;
			//cout << "Initial Energy  : " << initialEnergy << " eV" << endl;
			//cout << "Initial position: " << initialPosition.x() << ", " << initialPosition.y()  << ", " << initialPosition.z() << " cm" << endl;
			avalanchemicroscopic->AvalancheElectron(initialPosition.x(), initialPosition.y(), initialPosition.z(), initialTime, initialEnergy, initialDirection.x(), initialDirection.y(), initialDirection.z());

			Int_t ne, ni;
			avalanchemicroscopic->GetAvalancheSize(ne, ni);
			nele = ne;

			// local variables to be pushed into vectors
			Double_t xi, yi, zi, ti, ei;
			Double_t xf, yf, zf, tf, ef;
			Int_t stat;

			// number of electron endpoints - 1 is the number of hits on the readout for an event passing the mesh
			int np = avalanchemicroscopic->GetNumberOfElectronEndpoints();
			nelep = np;
			//cout << "Number of electron endpoints: " << np << endl;

			for (int j=0; j<np; j++) {
				avalanchemicroscopic->GetElectronEndpoint(j, xi, yi, zi, ti, ei, xf, yf, zf, tf, ef, stat);

				x0.push_back(xi); y0.push_back(yi); z0.push_back(zi); t0.push_back(ti); e0.push_back(ei);
				x1.push_back(xf); y1.push_back(yf); z1.push_back(zf); t1.push_back(tf); e1.push_back(ef);
				status.push_back(stat);
			}

			cout << setw(5) << i/(double)numberOfEvents*100. << "% of all events done." << endl;
			cout << setw(4) << e/(double)numberOfElectrons*100. << "% of this event done." << endl;
		}

		outputTree->Fill();
		x0.clear(); y0.clear(); z0.clear(); e0.clear(); t0.clear();
		x1.clear(); y1.clear(); z1.clear(); e1.clear(); t1.clear();
	}

	outputFile->cd();
	outputFile->Write();
	outputFile->Close();
	inputFile->Close();

	/*
	viewdrift->Plot();
	app.Run(kFALSE);
	*/

	cout << "Done." << endl;
	return 0;
}
