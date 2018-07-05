#include "FixationController.h"
#include <OpenSim/Simulation/Model/Model.h>
#include <OpenSim/Simulation/Model/Muscle.h>

using namespace std;
using namespace OpenSim;
using namespace SimTK;

/**
* Implementation of smooth sigmoid function.
*
* Parameters
* ----------
* t : time to be evaluated
* t0 : delay
* A : magnitude
* B : slope
*
* Returns
* -------
* (y, y', y'')
*/
Vec3 sigmoid(double t, double t0, double A, double B) {
    return Vec3(A * (tanh(B * (t - t0)) + 1) / 2,
                A * B * (1 - pow(tanh(B * (t - t0)), 2)) / 2,
                A * pow(B, 2) * tanh(B * (t - t0)) * (1 - pow(tanh(B * (t - t0)), 2)));
}

FixationController::FixationController() : Controller() {
    constructProperties();
}

void FixationController::constructProperties() {
    constructProperty_thetaH(0);
    constructProperty_thetaV(0);
    constructProperty_kpH(50);
    constructProperty_kdH(1.5);
    constructProperty_kpV(50);
    constructProperty_kdV(1.5);
    constructProperty_kpT(100);
    constructProperty_kdT(0.5);
    constructProperty_saccade_onset(0);
    constructProperty_saccade_velocity(200);
}

void FixationController::computeControls(const State& s, Vector& controls) const {
    double t = s.getTime();

    // Get model coordinate
    const auto& yCoord = _model->getCoordinateSet().get("r_eye_add_abd");
    const auto& zCoord = _model->getCoordinateSet().get("r_eye_sup_inf");
    const auto& xCoord = _model->getCoordinateSet().get("r_eye_inc_exc");

    // Get model muscles
    const auto& lat_rect = _model->getMuscles().get("r_Lateral_Rectus");
    const auto& med_rect = _model->getMuscles().get("r_Medial_Rectus");
    const auto& sup_rect = _model->getMuscles().get("r_Superior_Rectus");
    const auto& inf_rect = _model->getMuscles().get("r_Inferior_Rectus");
    const auto& sup_oblq = _model->getMuscles().get("r_Superior_Oblique");
    const auto& inf_oblq = _model->getMuscles().get("r_Inferior_Oblique");

    // Compute the desired position, velocity and acceleration
    double xdes = 0;
    double xdesv = 0;
    double xdesa = 0;
    auto sigmY = sigmoid(t,
                         get_saccade_onset(),
                         convertDegreesToRadians(get_thetaH()),
                         get_saccade_velocity());
    double ydes = 0 + sigmY[0];
    double ydesv = sigmY[1];
    double ydesa = sigmY[2];
    auto sigmZ = sigmoid(t,
                         get_saccade_onset(),
                         convertDegreesToRadians(get_thetaV()),
                         get_saccade_velocity());
    double zdes = 0 + sigmZ[0];
    double zdesv = sigmZ[1];
    double zdesa = sigmZ[2];

    // Get the current position and velocity
    double x = xCoord.getValue(s); double xv = xCoord.getSpeedValue(s);
    double y = yCoord.getValue(s); double yv = yCoord.getSpeedValue(s);
    double z = zCoord.getValue(s); double zv = zCoord.getSpeedValue(s);

    // The sum of errors are used as Excitation levels in the model
    double sumErrX = 0 * xdesa + get_kpT() * (xdes - x) + get_kdT() * (xdesv - xv);
    double sumErrY = 0 * ydesa + get_kpH() * (ydes - y) + get_kdH() * (ydesv - yv);
    double sumErrZ = 0 * zdesa + get_kpV() * (zdes - z) + get_kdV() * (zdesv - zv);

    // If desired force is in direction of one muscle's pull direction, then
    // set that muscle's control based on the position and velocity error,
    // otherwise set the muscle's control to zero
    double leftControl = 0.0, rightControl = 0.0;
    if (sumErrY < 0) {
        leftControl = abs(sumErrY);
        rightControl = 0.0;
    } else if (sumErrY > 0) {
        leftControl = 0.0;
        rightControl = abs(sumErrY);
    }
    double upControl = 0.0, downControl = 0.0;
    if (sumErrZ > 0) {
        upControl = abs(sumErrZ);
        downControl = 0.0;
    } else if (sumErrZ < 0) {
        upControl = 0.0;
        downControl = abs(sumErrZ);
    }
    double upTorControl = 0.0, downTorControl = 0.0;
    if (sumErrX < 0) {
        upTorControl = abs(sumErrX);
        downTorControl = 0.0;
    } else if (sumErrX > 0) {
        upTorControl = 0.0;
        downTorControl = abs(sumErrX);
    }

    // Set the activation inputs to the model Millard muscle has only one
    // control
    Vector muscleControl(1, leftControl); // left control -> Lateral Rectus
    lat_rect.addInControls(muscleControl, controls);

    muscleControl[0] = rightControl; // right control -> Medial Rectus
    med_rect.addInControls(muscleControl, controls);

    muscleControl[0] = upControl; // Up control -> Superior Rectus
    sup_rect.addInControls(muscleControl, controls);

    muscleControl[0] = downControl; // Down control -> Inferior Rectus
    inf_rect.addInControls(muscleControl, controls);

    muscleControl[0] = upTorControl; // Up Torsion Control -> Superior Oblique
    sup_oblq.addInControls(muscleControl, controls);

    muscleControl[0] = downTorControl; // Down Torsion Control -> Inferior Oblique
    inf_oblq.addInControls(muscleControl, controls);
}