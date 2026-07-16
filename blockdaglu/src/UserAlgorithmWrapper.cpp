#include "UserAlgorithmWrapper.h"

UserAlgorithmWrapper::UserAlgorithmWrapper()
    : solver_(std::make_unique<blockdaglu::Solver>()) {}

UserAlgorithmWrapper::~UserAlgorithmWrapper() = default;

bool UserAlgorithmWrapper::prepareData(COOMatrix m) {
  return solver_->set_matrix(std::move(m));
}

bool UserAlgorithmWrapper::rearrangeMatrix() {
  return solver_->analyze();
}

bool UserAlgorithmWrapper::decompositionLU() {
  return solver_->factor();
}

bool UserAlgorithmWrapper::solveLinearSystem(std::vector<double>& b) {
  return solver_->solve_in_place(b);
}

COOMatrix UserAlgorithmWrapper::getRearrangedMatrix() {
  return solver_->get_rearranged_matrix();
}

COOMatrix UserAlgorithmWrapper::getQ() {
  return solver_->getQ();
}

COOMatrix UserAlgorithmWrapper::getP() {
  return solver_->getP();
}

COOMatrix UserAlgorithmWrapper::getL() {
  return solver_->getL();
}

COOMatrix UserAlgorithmWrapper::getU() {
  return solver_->getU();
}

