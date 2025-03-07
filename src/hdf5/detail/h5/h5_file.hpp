#ifndef PHARE_HDF5_H5FILE_HPP
#define PHARE_HDF5_H5FILE_HPP

#include "highfive/H5File.hpp"
#include "highfive/H5Easy.hpp"

namespace PHARE::hdf5::h5
{
using HiFile = HighFive::File;


/*
  Highfive cannot accept a single flat array into >= 2d shaped datasets
*/
template<std::size_t dim, typename Data>
auto pointer_dim_caster(Data* data)
{
    if constexpr (dim == 1)
        return data;
    if constexpr (dim == 2)
        return reinterpret_cast<Data const** const>(data);
    if constexpr (dim == 3)
        return reinterpret_cast<Data const*** const>(data);
}
template<std::size_t dim, typename Data>
auto pointer_dim_caster(Data const* const data)
{
    if constexpr (dim == 1)
        return data;
    if constexpr (dim == 2)
        return reinterpret_cast<Data const* const* const>(data);
    if constexpr (dim == 3)
        return reinterpret_cast<Data const* const* const* const>(data);
}



template<std::size_t dim, typename Data>
auto decay_to_pointer(Data& data)
{
    if constexpr (dim == 1)
        return data.data();
    if constexpr (dim == 2)
        return data[0].data();
    if constexpr (dim == 3)
        return data[0][0].data();
}

template<typename Data, std::size_t dim>
auto vector_for_dim()
{
    if constexpr (dim == 1)
        return std::vector<Data>();
    if constexpr (dim == 2)
        return std::vector<std::vector<Data>>();
    if constexpr (dim == 3)
        return std::vector<std::vector<std::vector<Data>>>();
}

class HighFiveFile
{
public:
    static auto createHighFiveFile(std::string const path, unsigned flags, bool para)
    {
        if (para)
            return HiFile
            {
                path, flags
#if defined(H5_HAVE_PARALLEL)
                    ,
                    HighFive::MPIOFileDriver(MPI_COMM_WORLD, MPI_INFO_NULL)
#endif
            };
        return HiFile{path, flags};
    }

    HighFiveFile(std::string const path, unsigned flags = HiFile::ReadWrite, bool para = true)
        : h5file_{createHighFiveFile(path, flags, para)}
    {
    }

    ~HighFiveFile() {}

    HiFile& file() { return h5file_; }


    template<typename T, std::size_t dim = 1>
    auto read_data_set_flat(std::string path) const
    {
        std::vector<T> data(H5Easy::getSize(h5file_, path));
        h5file_.getDataSet(path).read(pointer_dim_caster<dim>(data.data()));
        return data;
    }

    template<typename T, std::size_t dim = 1>
    auto read_data_set(std::string path) const
    {
        auto data = vector_for_dim<T, dim>();
        h5file_.getDataSet(path).read(data);
        return data;
    }


    template<std::size_t dim = 1, typename Data>
    auto& write_data_set(std::string path, Data const& data)
    {
        h5file_.getDataSet(path).write(data);
        return *this;
    }

    template<std::size_t dim = 1, typename Data>
    auto& write_data_set_flat(std::string path, Data const& data)
    {
        h5file_.getDataSet(path).write(pointer_dim_caster<dim>(data));
        return *this;
    }


    template<typename Type, typename Size>
    void create_data_set(std::string const& path, Size const& dataSetSize)
    {
        createGroupsToDataSet(path);
        h5file_.createDataSet<Type>(path, HighFive::DataSpace(dataSetSize));
    }



    /*
     * Communicate all dataset paths and sizes to all MPI process to allow each to create all
     * datasets independently. This is a requirement of HDF5.
     * in the case of a disparate number of datasets per MPI process, dataSetSize may be 0
     * such that the current process creates datasets for all other processes with non-zero
     * sizes. Recommended to use similar sized paths, if possible.
     */
    template<typename Type, typename Size>
    void create_data_set_per_mpi(std::string const& path, Size const& dataSetSize)
    {
        auto mpi_size = core::mpi::size();
        auto sizes    = core::mpi::collect(dataSetSize, mpi_size);
        auto paths    = core::mpi::collect(path, mpi_size);
        for (int i = 0; i < mpi_size; i++)
        { // in the case an mpi node lacks something to write
            if (is_zero(sizes[i]) || paths[i] == "")
                continue;

            create_data_set<Type>(paths[i], sizes[i]);
        }
    }



    template<typename Data>
    void write_attribute(std::string const& path, std::string const& key, Data const& value)
    {
        h5file_.getGroup(path)
            .template createAttribute<Data>(key, HighFive::DataSpace::From(value))
            .write(value);
    }



    /*
     * Communicate all attribute paths and values to all MPI process to allow each to create all
     * attributes independently. This is a requirement of HDF5.
     * in the case of a disparate number of attributes per MPI process, path may be an empty string
     * such that the current process creates attributes for all other processes with non-zero
     * sizes. Recommended to use similar sized paths, if possible. key is always assumed to the be
     * the same
     */
    template<typename Data>
    void write_attributes_per_mpi(std::string path, std::string key, Data const& data)
    {
        constexpr bool data_is_vector = core::is_std_vector_v<Data>;

        auto doAttribute = [&](auto node, auto const& _key, auto const& value) {
            if constexpr (data_is_vector)
            {
                if (value.size())
                    node.template createAttribute<typename Data::value_type>(
                            _key, HighFive::DataSpace(value.size()))
                        .write(value.data());
            }
            else
                node.template createAttribute<Data>(_key, HighFive::DataSpace::From(value))
                    .write(value);
        };

        int mpi_size = core::mpi::size();
        auto values  = [&]() {
            if constexpr (data_is_vector)
                return core::mpi::collect_raw(data, mpi_size);
            else
                return core::mpi::collect(data, mpi_size);
        }();
        auto paths = core::mpi::collect(path, mpi_size);

        for (int i = 0; i < mpi_size; i++)
        {
            std::string const keyPath = paths[i] == "null" ? "" : paths[i];
            if (keyPath.empty())
                continue;

            if (h5file_.exist(keyPath)
                && h5file_.getObjectType(keyPath) == HighFive::ObjectType::Dataset)
            {
                if (!h5file_.getDataSet(keyPath).hasAttribute(key))
                    doAttribute(h5file_.getDataSet(keyPath), key, values[i]);
            }
            else // group
            {
                createGroupsToDataSet(keyPath + "/dataset");
                if (!h5file_.getGroup(keyPath).hasAttribute(key))
                    doAttribute(h5file_.getGroup(keyPath), key, values[i]);
            }
        }
    }


    HighFiveFile(const HighFiveFile&)  = delete;
    HighFiveFile(const HighFiveFile&&) = delete;
    HighFiveFile& operator=(const HighFiveFile&) = delete;
    HighFiveFile& operator=(const HighFiveFile&&) = delete;

private:
    HiFile h5file_;


    // during attribute/dataset creation, we currently don't require the parents of the group to
    // exist, but create all that are missing - this used to exist in highfive
    inline std::string getParentName(std::string const& path)
    {
        std::size_t idx = path.find_last_of("/");
        if (idx == std::string::npos or idx == 0)
            return "/";
        return path.substr(0, idx);
    }

    inline void createGroupsToDataSet(std::string const& path)
    {
        std::string group_name = getParentName(path);
        if (!h5file_.exist(group_name))
            h5file_.createGroup(group_name);
    }



    template<typename Size>
    static bool is_zero(Size size)
    {
        if constexpr (core::is_iterable_v<Size>)
            return std::all_of(size.begin(), size.end(), [](auto const& val) { return val == 0; });

        else
            return size == 0;
    }
};




} // namespace PHARE::hdf5::h5


#endif /* PHARE_HDF5_H5FILE_HPP */
