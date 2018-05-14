#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <random>

#include <core/data/electromag/electromag.h>
#include <core/data/field/field.h>
#include <core/data/ndarray/ndarray_vector.h>
#include <core/data/particles/particle.h>
#include <core/data/particles/particle_array.h>
#include <core/data/vecfield/vecfield.h>
#include <core/hybrid/hybrid_quantities.h>
#include <numerics/interpolator/interpolator.h>


using PHARE::computeStartIndex;
using PHARE::Electromag;
using PHARE::Field;
using PHARE::HybridQuantity;
using PHARE::Layout;
using PHARE::nbrPointsSupport;
using PHARE::NdArrayVector1D;
using PHARE::NdArrayVector2D;
using PHARE::NdArrayVector3D;
using PHARE::Particle;
using PHARE::VecField;
using PHARE::Weighter;



template<typename Weighter>
class AWeighter : public ::testing::Test
{
public:
    AWeighter()
    {
        std::random_device rd;  // Will be used to obtain a seed for the random number engine
        std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
        std::uniform_real_distribution<> dis(5, 10);


        // generate the nbr_tests random (normalized) particle position
        std::generate(std::begin(normalizedPositions), std::end(normalizedPositions),
                      [&dis, &gen]() { return dis(gen); });


        // now for each random position, calculate
        // the start index and the N(interp_order) weights
        for (auto i = 0u; i < nbr_tests; ++i)
        {
            auto startIndex = computeStartIndex<Weighter::interp_order>(normalizedPositions[i]);
            this->weighter.computeWeight(normalizedPositions[i], startIndex, weights[i]);
        }


        // for each position, we obtained N(interp_order) weights
        // store their in weightsSum
        std::transform(std::begin(weights), std::end(weights), std::begin(weightsSums),
                       [](auto const& weight_list) {
                           return std::accumulate(std::begin(weight_list), std::end(weight_list),
                                                  0.);
                       });
    }

protected:
    Weighter weighter;
    static const int nbr_tests = 100000;
    std::array<double, nbr_tests> normalizedPositions;
    std::array<double, nbr_tests> weightsSums;
    std::array<std::array<double, nbrPointsSupport(Weighter::interp_order)>, nbr_tests> weights;
};


using Weighters = ::testing::Types<Weighter<1>, Weighter<2>, Weighter<3>>;




TYPED_TEST_CASE(AWeighter, Weighters);


TYPED_TEST(AWeighter, ComputesWeightThatSumIsOne)
{
    auto equalsOne = [](double sum) { return std::abs(sum - 1.) < 1e-10; };
    EXPECT_TRUE(std::all_of(std::begin(this->weightsSums), std::end(this->weightsSums), equalsOne));
}



TEST(Weights, NbrPointsInBSplineSupportIsCorrect)
{
    EXPECT_EQ(2, nbrPointsSupport(1));
    EXPECT_EQ(3, nbrPointsSupport(2));
    EXPECT_EQ(4, nbrPointsSupport(3));
}




struct BSpline
{
    std::vector<std::vector<double>> weights;
    std::vector<std::vector<int>> indexes;
};


// this function reads the indices and weights
// from the file generated by the python script interpolator_test.py
BSpline readFromFile(std::size_t order)
{
    std::ostringstream oss;
    oss << "bsplines_" << order << ".dat";

    std::ifstream file{oss.str()};
    if (!file)
    {
        std::cout << "file not found\n";
    }

    BSpline bs;
    bs.indexes.resize(10);
    bs.weights.resize(10);

    for (auto ipos = 0u; ipos < 10; ++ipos)
    {
        bs.indexes[ipos].resize(order + 1);
        bs.weights[ipos].resize(order + 1);

        file.read(reinterpret_cast<char*>(bs.indexes[ipos].data()),
                  static_cast<std::streamsize>(bs.indexes[ipos].size() * sizeof(int)));

        file.read(reinterpret_cast<char*>(bs.weights[ipos].data()),
                  static_cast<std::streamsize>(bs.weights[ipos].size() * sizeof(double)));
    }
    return bs;
}


TYPED_TEST(AWeighter, computesBSplineWeightsForAnyParticlePosition)
{
    auto data = readFromFile(TypeParam::interp_order);

    std::array<double, nbrPointsSupport(TypeParam::interp_order)> weights;

    // python file hard-codes 10 particle positions
    // that start at x = 3
    const int particlePositionNbr = 10;
    const double startPosition    = 3.;
    std::vector<double> particle_positions(particlePositionNbr);
    double dx = 0.1;

    for (auto i = 0u; i < particlePositionNbr; ++i)
    {
        particle_positions[i] = startPosition + i * dx;
    }

    for (auto ipos = 0u; ipos < particlePositionNbr; ++ipos)
    {
        auto pos = particle_positions[ipos];

        auto startIndex = computeStartIndex<TypeParam::interp_order>(pos);
        this->weighter.computeWeight(pos, startIndex, weights);

        for (auto inode = 0u; inode < weights.size(); ++inode)
        {
            EXPECT_DOUBLE_EQ(data.weights[ipos][inode], weights[inode]);
            std::cout << inode << " " << ipos << " " << data.weights[ipos][inode] << " =? "
                      << weights[inode] << "\n";
        }
    }
}



template<typename InterpolatorT>
class A1DInterpolator : public ::testing::Test
{
public:
    using VF = VecField<NdArrayVector1D<>, HybridQuantity>;
    Electromag<VF> em;
    PHARE::ParticleArray<1> particles;
    InterpolatorT interp;

    static constexpr uint32_t nx = 50;

    Field<NdArrayVector1D<>, typename HybridQuantity::Scalar> bx1d_;
    Field<NdArrayVector1D<>, typename HybridQuantity::Scalar> by1d_;
    Field<NdArrayVector1D<>, typename HybridQuantity::Scalar> bz1d_;
    Field<NdArrayVector1D<>, typename HybridQuantity::Scalar> ex1d_;
    Field<NdArrayVector1D<>, typename HybridQuantity::Scalar> ey1d_;
    Field<NdArrayVector1D<>, typename HybridQuantity::Scalar> ez1d_;

    static constexpr double ex0 = 2.25;
    static constexpr double ey0 = 2.50;
    static constexpr double ez0 = 2.75;
    static constexpr double bx0 = 2.25;
    static constexpr double by0 = 2.50;
    static constexpr double bz0 = 2.75;

    A1DInterpolator()
        : em{"EM"}
        , particles(5)
        , bx1d_{"field", HybridQuantity::Scalar::Bx, nx}
        , by1d_{"field", HybridQuantity::Scalar::By, nx}
        , bz1d_{"field", HybridQuantity::Scalar::Bz, nx}
        , ex1d_{"field", HybridQuantity::Scalar::Ex, nx}
        , ey1d_{"field", HybridQuantity::Scalar::Ey, nx}
        , ez1d_{"field", HybridQuantity::Scalar::Ez, nx}
    {
        for (auto ix = 0u; ix < nx; ++ix)
        {
            bx1d_(ix) = bx0;
            by1d_(ix) = by0;
            bz1d_(ix) = bz0;

            ex1d_(ix) = ex0;
            ey1d_(ix) = ey0;
            ez1d_(ix) = ez0;
        }

        for (auto& part : particles)
        {
            part.iCell[0] = 5;
            part.delta[0] = 0.32f;
        }
    }
};




using Interpolators1D = ::testing::Types<PHARE::Interpolator<1, 1, Layout::Yee>,
                                         PHARE::Interpolator<1, 2, Layout::Yee>,
                                         PHARE::Interpolator<1, 3, Layout::Yee>>;

TYPED_TEST_CASE(A1DInterpolator, Interpolators1D);



TYPED_TEST(A1DInterpolator, canComputeAllEMfieldsAtParticle)
{
    this->em.E.setBuffer("EM_E_x", &this->ex1d_);
    this->em.E.setBuffer("EM_E_y", &this->ey1d_);
    this->em.E.setBuffer("EM_E_z", &this->ez1d_);
    this->em.B.setBuffer("EM_B_x", &this->bx1d_);
    this->em.B.setBuffer("EM_B_y", &this->by1d_);
    this->em.B.setBuffer("EM_B_z", &this->bz1d_);

    this->interp(std::begin(this->particles), std::end(this->particles), this->em);

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<1> const& part) { return std::abs(part.Ex - this->ex0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<1> const& part) { return std::abs(part.Ey - this->ey0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<1> const& part) { return std::abs(part.Ez - this->ez0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<1> const& part) { return std::abs(part.Bx - this->bx0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<1> const& part) { return std::abs(part.By - this->by0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<1> const& part) { return std::abs(part.Bz - this->bz0) < 1e-8; }));


    this->em.E.setBuffer("EM_E_x", nullptr);
    this->em.E.setBuffer("EM_E_y", nullptr);
    this->em.E.setBuffer("EM_E_z", nullptr);
    this->em.B.setBuffer("EM_B_x", nullptr);
    this->em.B.setBuffer("EM_B_y", nullptr);
    this->em.B.setBuffer("EM_B_z", nullptr);
}




template<typename InterpolatorT>
class A2DInterpolator : public ::testing::Test
{
public:
    using VF = VecField<NdArrayVector2D<>, HybridQuantity>;
    Electromag<VF> em;
    PHARE::ParticleArray<2> particles;
    InterpolatorT interp;

    static constexpr uint32_t nx = 50;
    static constexpr uint32_t ny = 50;

    Field<NdArrayVector2D<>, typename HybridQuantity::Scalar> bx_;
    Field<NdArrayVector2D<>, typename HybridQuantity::Scalar> by_;
    Field<NdArrayVector2D<>, typename HybridQuantity::Scalar> bz_;
    Field<NdArrayVector2D<>, typename HybridQuantity::Scalar> ex_;
    Field<NdArrayVector2D<>, typename HybridQuantity::Scalar> ey_;
    Field<NdArrayVector2D<>, typename HybridQuantity::Scalar> ez_;

    static constexpr double ex0 = 2.25;
    static constexpr double ey0 = 2.50;
    static constexpr double ez0 = 2.75;
    static constexpr double bx0 = 2.25;
    static constexpr double by0 = 2.50;
    static constexpr double bz0 = 2.75;

    A2DInterpolator()
        : em{"EM"}
        , particles(5)
        , bx_{"field", HybridQuantity::Scalar::Bx, nx, ny}
        , by_{"field", HybridQuantity::Scalar::By, nx, ny}
        , bz_{"field", HybridQuantity::Scalar::Bz, nx, ny}
        , ex_{"field", HybridQuantity::Scalar::Ex, nx, ny}
        , ey_{"field", HybridQuantity::Scalar::Ey, nx, ny}
        , ez_{"field", HybridQuantity::Scalar::Ez, nx, ny}
    {
        for (auto ix = 0u; ix < nx; ++ix)
        {
            for (auto iy = 0u; iy < ny; ++iy)
            {
                bx_(ix, iy) = bx0;
                by_(ix, iy) = by0;
                bz_(ix, iy) = bz0;
                ex_(ix, iy) = ex0;
                ey_(ix, iy) = ey0;
                ez_(ix, iy) = ez0;
            }
        }

        for (auto& part : particles)
        {
            part.iCell[0] = 5;
            part.delta[0] = 0.32f;
        }
    }
};




using Interpolators2D = ::testing::Types<PHARE::Interpolator<2, 1, Layout::Yee>,
                                         PHARE::Interpolator<2, 2, Layout::Yee>,
                                         PHARE::Interpolator<2, 3, Layout::Yee>>;

TYPED_TEST_CASE(A2DInterpolator, Interpolators2D);



TYPED_TEST(A2DInterpolator, canComputeAllEMfieldsAtParticle)
{
    this->em.E.setBuffer("EM_E_x", &this->ex_);
    this->em.E.setBuffer("EM_E_y", &this->ey_);
    this->em.E.setBuffer("EM_E_z", &this->ez_);
    this->em.B.setBuffer("EM_B_x", &this->bx_);
    this->em.B.setBuffer("EM_B_y", &this->by_);
    this->em.B.setBuffer("EM_B_z", &this->bz_);

    this->interp(std::begin(this->particles), std::end(this->particles), this->em);

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<2> const& part) { return std::abs(part.Ex - this->ex0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<2> const& part) { return std::abs(part.Ey - this->ey0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<2> const& part) { return std::abs(part.Ez - this->ez0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<2> const& part) { return std::abs(part.Bx - this->bx0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<2> const& part) { return std::abs(part.By - this->by0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<2> const& part) { return std::abs(part.Bz - this->bz0) < 1e-8; }));


    this->em.E.setBuffer("EM_E_x", nullptr);
    this->em.E.setBuffer("EM_E_y", nullptr);
    this->em.E.setBuffer("EM_E_z", nullptr);
    this->em.B.setBuffer("EM_B_x", nullptr);
    this->em.B.setBuffer("EM_B_y", nullptr);
    this->em.B.setBuffer("EM_B_z", nullptr);
}




template<typename InterpolatorT>
class A3DInterpolator : public ::testing::Test
{
public:
    using VF = VecField<NdArrayVector3D<>, HybridQuantity>;
    Electromag<VF> em;
    PHARE::ParticleArray<3> particles;
    InterpolatorT interp;

    static constexpr uint32_t nx = 50;
    static constexpr uint32_t ny = 50;
    static constexpr uint32_t nz = 50;

    Field<NdArrayVector3D<>, typename HybridQuantity::Scalar> bx_;
    Field<NdArrayVector3D<>, typename HybridQuantity::Scalar> by_;
    Field<NdArrayVector3D<>, typename HybridQuantity::Scalar> bz_;
    Field<NdArrayVector3D<>, typename HybridQuantity::Scalar> ex_;
    Field<NdArrayVector3D<>, typename HybridQuantity::Scalar> ey_;
    Field<NdArrayVector3D<>, typename HybridQuantity::Scalar> ez_;

    static constexpr double ex0 = 2.25;
    static constexpr double ey0 = 2.50;
    static constexpr double ez0 = 2.75;
    static constexpr double bx0 = 2.25;
    static constexpr double by0 = 2.50;
    static constexpr double bz0 = 2.75;

    A3DInterpolator()
        : em{"EM"}
        , particles(5)
        , bx_{"field", HybridQuantity::Scalar::Bx, nx, ny, nz}
        , by_{"field", HybridQuantity::Scalar::By, nx, ny, nz}
        , bz_{"field", HybridQuantity::Scalar::Bz, nx, ny, nz}
        , ex_{"field", HybridQuantity::Scalar::Ex, nx, ny, nz}
        , ey_{"field", HybridQuantity::Scalar::Ey, nx, ny, nz}
        , ez_{"field", HybridQuantity::Scalar::Ez, nx, ny, nz}
    {
        for (auto ix = 0u; ix < nx; ++ix)
        {
            for (auto iy = 0u; iy < ny; ++iy)
            {
                for (auto iz = 0u; iz < nz; ++iz)
                {
                    bx_(ix, iy, iz) = bx0;
                    by_(ix, iy, iz) = by0;
                    bz_(ix, iy, iz) = bz0;
                    ex_(ix, iy, iz) = ex0;
                    ey_(ix, iy, iz) = ey0;
                    ez_(ix, iy, iz) = ez0;
                }
            }
        }

        for (auto& part : particles)
        {
            part.iCell[0] = 5;
            part.delta[0] = 0.32f;
        }
    }
};




using Interpolators3D = ::testing::Types<PHARE::Interpolator<3, 1, Layout::Yee>,
                                         PHARE::Interpolator<3, 2, Layout::Yee>,
                                         PHARE::Interpolator<3, 3, Layout::Yee>>;

TYPED_TEST_CASE(A3DInterpolator, Interpolators3D);



TYPED_TEST(A3DInterpolator, canComputeAllEMfieldsAtParticle)
{
    this->em.E.setBuffer("EM_E_x", &this->ex_);
    this->em.E.setBuffer("EM_E_y", &this->ey_);
    this->em.E.setBuffer("EM_E_z", &this->ez_);
    this->em.B.setBuffer("EM_B_x", &this->bx_);
    this->em.B.setBuffer("EM_B_y", &this->by_);
    this->em.B.setBuffer("EM_B_z", &this->bz_);

    this->interp(std::begin(this->particles), std::end(this->particles), this->em);

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<3> const& part) { return std::abs(part.Ex - this->ex0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<3> const& part) { return std::abs(part.Ey - this->ey0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<3> const& part) { return std::abs(part.Ez - this->ez0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<3> const& part) { return std::abs(part.Bx - this->bx0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<3> const& part) { return std::abs(part.By - this->by0) < 1e-8; }));

    EXPECT_TRUE(std::all_of(
        std::begin(this->particles), std::end(this->particles),
        [this](Particle<3> const& part) { return std::abs(part.Bz - this->bz0) < 1e-8; }));


    this->em.E.setBuffer("EM_E_x", nullptr);
    this->em.E.setBuffer("EM_E_y", nullptr);
    this->em.E.setBuffer("EM_E_z", nullptr);
    this->em.B.setBuffer("EM_B_x", nullptr);
    this->em.B.setBuffer("EM_B_y", nullptr);
    this->em.B.setBuffer("EM_B_z", nullptr);
}



int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
