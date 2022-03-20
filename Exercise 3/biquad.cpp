#include <CL/sycl.hpp>
#include <iostream>
#include <limits>

#include "dpc_common.hpp"

using namespace std;
using namespace sycl;

/**
 * The result is a 1x1 matrix i.e a single number as
 * 1xN * NxN = 1xN matrix * Nx1 = 1x1 matrix
 * Multiply v^T * b first then multiply by v
 */

 // Vector size constants.
constexpr int vec_size = 250 * 8;  // Must be a multiple of 8.
constexpr int n = vec_size / 8;

/**
 * Perform matrix multiplication on host to verify results from device.
 */
int VerifyResult(float(*c_back));

int main() {
    // Host memory buffer that device will write data back before destruction.
    float(*c_back) = new float;

    // Intialize c_back
    *c_back = 0.0f;

    // Initialize the device queue with the default selector. The device queue is
    // used to enqueue kernels. It encapsulates all states needed for execution.
    try {
        queue q(cpu_selector{}, dpc_common::exception_handler);

        std::cout << "Device: " << q.get_device().get_info<info::device::name>() << "\n";

        // Create buffers for vector and matrix, buffer c is bound with host memory c_back

        buffer<float, 1> v_buf{ range<1>(n) };
        buffer<float, 1> t_buf{ range<1>(n) };
        buffer<float, 2> b_buf{ range<2>(n, n) };
        buffer<float, 1> c_buf{ reinterpret_cast<float*>(c_back), range<1>(1) };

        std::cout << "Problem size: c(" << 1 << "," << 1 << ") = v^T(" << 1 << "," << n
            << ") * b(" << n << "," << n << ")" << "* v(" << n << "," << "1" << ")\n";

        // Using three command groups to illustrate execution order. The use of
        // first two command groups for initializing matrices is not the most
        // efficient way. It just demonstrates the implicit multiple command group
        // execution ordering.

        // Submit command group to queue to initialize vector v
        q.submit([&](auto& h) {
            // Get write only access to the buffer on a device.
            accessor v(v_buf, h, write_only);

            // Execute kernel.
            h.parallel_for(range(n), [=](auto index) {
                // Each element of vector v is index.
                v[index] = index;
                });
            });

        // Submit command group to queue to initialize matrix b
        q.submit([&](auto& h) {
            // Get write only access to the buffer on a device
            accessor b(b_buf, h, write_only);

            // Execute kernel.
            h.parallel_for(range(n, n), [=](auto index) {
                // Each element of b is i + j
                b[index] = index[0] + index[1];
                });
            });

        // Submit command group to queue to multiply matrices: t = v^T * b
        q.submit([&](auto& h) {
            // Read from v and b, write to t
            accessor v(v_buf, h, read_only);
            accessor b(b_buf, h, read_only);
            accessor t(t_buf, h, write_only);

            // Execute kernel.
            h.parallel_for(range(1, n), [=](auto index) {
                // Get global position in Y direction.
                int row = index[0];
                // Get global position in X direction.
                int col = index[1];

                float sum = 0.0f;

                // Compute the result of one element of t
                for (int i = 0; i < n; i++) {
                    sum += v[i] * b[i][col];
                }

                t[col] = sum;
                });
            });

        // Submit command group to queue to multiply matrices: c = t * v
        q.submit([&](auto& h) {
            // Read from t and v, write to c
            accessor v(v_buf, h, read_only);
            accessor t(t_buf, h, read_only);
            accessor c(c_buf, h, write_only);

            // Execute kernel.
            h.parallel_for(range(1), [=](auto index) {

                float sum = 0.0f;

                // Compute the result of c
                for (int i = 0; i < n; i++) {
                    sum += v[i] * t[i];
                }

                c[index] = sum;

                });
            });
    }
    catch (sycl::exception const& e) {
        std::cout << "An exception is caught while multiplying matrices.\n";
        terminate();
    }

    int result;
    std::cout << "Result of matrix multiplication using DPC++: ";
    result = VerifyResult(c_back);
    delete c_back;

    return result;
}

bool ValueSame(float a, float b, int ulp) {
    return fabs(a - b) < numeric_limits<float>::epsilon() * fabs(a + b) * ulp
        || fabs(a - b) < numeric_limits<float>::min();
}

int VerifyResult(float(*c_back)) {
    // Check that the results are correct by comparing with host computing.
    int i, j;

    // Vector and symmetric-matrix on host side.
    float(*v_host) = new float[n];
    float(*b_host)[n] = new float[n][n];
    float(*t_host) = new float[n];
    float(*c_host) = new float(0.0f);

    // v_host contains the sequence 0,1...N-1
    for (i = 0; i < n; i++) {
        v_host[i] = i;
        t_host[i] = 0.0f;
    }

    // b_host is initialized to i + j.
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) b_host[i][j] = i + j;

    // Compute t = v^T * b
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            t_host[i] += v_host[j] * b_host[j][i];
        }
    }

    // Compute c = t * v
    for (i = 0; i < n; i++) {
        (*c_host) += t_host[i] * v_host[i];
    }

    std::cout << "Host: " << *c_host << " Kernel: " << *c_back << "\n";
    bool mismatch_found = false;
    if (!ValueSame(*c_back, *c_host, 5)) {
        mismatch_found = true;
    }


    delete[] v_host;
    delete[] t_host;
    delete c_host;
    delete[] b_host;

    if (!mismatch_found) {
        std::cout << "Success - The results are correct!\n";
        return 0;
    }
    else {
        std::cout << "Fail - The results mismatch!\n";
        return -1;
    }
}
