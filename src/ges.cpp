#include "ges.h"
#include <boost/python/numpy.hpp>
#include <boost/scoped_array.hpp>
#include <iostream>
#include <vector>
#include "DecomposableScore.h"
#include "torch/torch.h"

using namespace std;
namespace p = boost::python;
namespace np = boost::python::numpy;

// Actual C++/C code here that does everything
void count_and_sum(double* array, int num, double output[]) {
    double sum = 0.0;

    for (int i = 0; i < num; ++i) {
        sum += array[i];
    }

    // Set output values
    output[0] = sum;
    output[1] = (double)num;
}

// A few translators here to ensure that numpy datatypes convert to pointers and
// what not
np::ndarray wrap_count_and_sum(np::ndarray const& array) {
    // Make sure we get doubles
    if (array.get_dtype() != np::dtype::get_builtin<double>()) {
        PyErr_SetString(PyExc_TypeError, "Incorrect array data type");
        p::throw_error_already_set();
    }

    // Could also pass back a vector, but unsure if you use C++ or C
    static double output[2];  // the static here is important, keeps it around!
    count_and_sum(reinterpret_cast<double*>(array.get_data()), array.shape(0),
                  output);

    // Turning the output into a numpy array
    np::dtype dt = np::dtype::get_builtin<double>();
    p::tuple shape = p::make_tuple(2);  // It has shape (2,)
    p::tuple stride =
        p::make_tuple(sizeof(double));  // 1D array, so its just size of double
    np::ndarray result = np::from_data(output, dt, shape, stride, p::object());
    return result;
}

torch::Tensor np_to_torch_double(const np::ndarray& array) {
    int s1 = array.shape(0), s2 = array.shape(1);
    auto narray = array.reshape(p::make_tuple(s1 * s2));
    double* arr_data = reinterpret_cast<double*>(narray.get_data());
    auto result =
        torch::tensor(std::vector<double>{arr_data, arr_data + s1 * s2});
    result = result.reshape({s1, s2});
    return result;
}

np::ndarray torch_to_np_int(const torch::Tensor& tensor_) {
    int s1 = tensor_.size(0), s2 = tensor_.size(1);
    auto tensor = tensor_.reshape({-1}).toType(torch::kInt).contiguous();
    std::vector<int> vec{tensor.data_ptr<int>(),
                         tensor.data_ptr<int>() + tensor.numel()};
    auto dt = np::dtype::get_builtin<int>();
    auto shape = p::make_tuple(s1 * s2);
    auto new_shape = p::make_tuple(s1, s2);
    auto stride = p::make_tuple(sizeof(int));
    auto result = np::from_data(vec.data(), dt, shape, stride, p::object())
                      .copy()
                      .reshape(new_shape);
    return result;
}

// Run GES Wrapper (array: p x n)
np::ndarray run_ges(const np::ndarray& array) {
    // Make sure we get doubles
    if (array.get_dtype() != np::dtype::get_builtin<double>()) {
        PyErr_SetString(PyExc_TypeError, "Incorrect array data type");
        p::throw_error_already_set();
    }
    if (array.get_nd() != 2) {
        PyErr_SetString(PyExc_TypeError, "dim != 2");
        p::throw_error_already_set();
    }
    // Convert np::ndarray to torch::Tensor
    auto&& tensor = np_to_torch_double(array);

    // Run GES
    auto n = tensor.size(1);
    auto A0 = torch::zeros({n, n}).toType(torch::kLong);
    auto score_class = GaussObsL0Pen(tensor);
    auto&& [result, score] =
        ges::fit(A0, score_class, {"forward", "backward"}, false, 0);

    // Convert torch::Tensor to np::ndarray
    auto&& result_np = torch_to_np_int(result);
    return result_np;
}

// Run GES Wrapper (array: p x n)
np::ndarray run_cluster_ges(const np::ndarray& array, const p::list& l) {
    // Make sure we get doubles
    if (array.get_dtype() != np::dtype::get_builtin<double>()) {
        PyErr_SetString(PyExc_TypeError, "Incorrect array data type");
        p::throw_error_already_set();
    }
    if (array.get_nd() != 2) {
        PyErr_SetString(PyExc_TypeError, "dim != 2");
        p::throw_error_already_set();
    }
    // Get graph data
    std::vector<std::vector<int>> graph;
    int l_len = (int)p::len(l);
    for (int i = 0; i < l_len; ++i) {
        std::vector<int> node;
        auto ll = l[i];
        auto ll_len = (int)p::len(ll);
        for (int j = 0; j < ll_len; ++j) {
            auto value = p::extract<int>(ll[j]);
            node.emplace_back(value);
        }
        graph.emplace_back(node);
    }

    // Convert np::ndarray to torch::Tensor
    auto&& tensor = np_to_torch_double(array);

    // Run GES
    auto n = tensor.size(1);
    auto A0 = torch::zeros({l_len, l_len}).toType(torch::kLong);
    auto score_class = GaussClusterL0Pen(tensor, graph);
    auto&& [result, score] =
        ges::fit(A0, score_class, {"forward", "backward"}, false, 0);

    // Convert torch::Tensor to np::ndarray
    auto&& result_np = torch_to_np_int(result);
    return result_np;
}

// Deciding what to expose in the library python can import
BOOST_PYTHON_MODULE(
    gescpp) {  // Thing in brackets should match output library name
    Py_Initialize();
    np::initialize();
    p::def("run_ges", run_ges);
    p::def("run_cluster_ges", run_cluster_ges);
}
