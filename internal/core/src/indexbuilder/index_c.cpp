// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License

#include <glog/logging.h>
#include <memory>
#include <string>
#include "fmt/core.h"
#include "indexbuilder/type_c.h"
#include "log/Log.h"
#include "storage/options.h"

#ifdef __linux__
#include <malloc.h>
#endif

#include "common/EasyAssert.h"
#include "indexbuilder/VecIndexCreator.h"
#include "indexbuilder/index_c.h"
#include "indexbuilder/IndexFactory.h"
#include "common/type_c.h"
#include "storage/Types.h"
#include "indexbuilder/types.h"
#include "index/Utils.h"
#include "pb/index_cgo_msg.pb.h"
#include "storage/Util.h"
#include "storage/space.h"
#include "index/Meta.h"

using namespace milvus;
CStatus
CreateIndexV0(enum CDataType dtype,
              const char* serialized_type_params,
              const char* serialized_index_params,
              CIndex* res_index) {
    auto status = CStatus();
    try {
        AssertInfo(res_index, "failed to create index, passed index was null");

        milvus::proto::indexcgo::TypeParams type_params;
        milvus::proto::indexcgo::IndexParams index_params;
        milvus::index::ParseFromString(type_params, serialized_type_params);
        milvus::index::ParseFromString(index_params, serialized_index_params);

        milvus::Config config;
        for (auto i = 0; i < type_params.params_size(); ++i) {
            const auto& param = type_params.params(i);
            config[param.key()] = param.value();
        }

        for (auto i = 0; i < index_params.params_size(); ++i) {
            const auto& param = index_params.params(i);
            config[param.key()] = param.value();
        }

        config[milvus::index::INDEX_ENGINE_VERSION] = std::to_string(
            knowhere::Version::GetCurrentVersion().VersionNumber());

        auto& index_factory = milvus::indexbuilder::IndexFactory::GetInstance();
        auto index =
            index_factory.CreateIndex(milvus::DataType(dtype),
                                      config,
                                      milvus::storage::FileManagerContext());

        *res_index = index.release();
        status.error_code = Success;
        status.error_msg = "";
    } catch (std::exception& e) {
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
    }
    return status;
}

CStatus
CreateIndex(CIndex* res_index, CBuildIndexInfo c_build_index_info) {
    try {
        auto build_index_info = (BuildIndexInfo*)c_build_index_info;
        auto field_type = build_index_info->field_type;

        milvus::index::CreateIndexInfo index_info;
        index_info.field_type = build_index_info->field_type;

        auto& config = build_index_info->config;
        config["insert_files"] = build_index_info->insert_files;

        // get index type
        auto index_type = milvus::index::GetValueFromConfig<std::string>(
            config, "index_type");
        AssertInfo(index_type.has_value(), "index type is empty");
        index_info.index_type = index_type.value();

        auto engine_version = build_index_info->index_engine_version;

        index_info.index_engine_version = engine_version;
        config[milvus::index::INDEX_ENGINE_VERSION] =
            std::to_string(engine_version);

        // get metric type
        if (milvus::datatype_is_vector(field_type)) {
            auto metric_type = milvus::index::GetValueFromConfig<std::string>(
                config, "metric_type");
            AssertInfo(metric_type.has_value(), "metric type is empty");
            index_info.metric_type = metric_type.value();
        }

        // init file manager
        milvus::storage::FieldDataMeta field_meta{
            build_index_info->collection_id,
            build_index_info->partition_id,
            build_index_info->segment_id,
            build_index_info->field_id};

        milvus::storage::IndexMeta index_meta{build_index_info->segment_id,
                                              build_index_info->field_id,
                                              build_index_info->index_build_id,
                                              build_index_info->index_version};
        auto chunk_manager = milvus::storage::CreateChunkManager(
            build_index_info->storage_config);

        milvus::storage::FileManagerContext fileManagerContext(
            field_meta, index_meta, chunk_manager);

        auto index =
            milvus::indexbuilder::IndexFactory::GetInstance().CreateIndex(
                build_index_info->field_type, config, fileManagerContext);
        index->Build();
        *res_index = index.release();
        auto status = CStatus();
        status.error_code = Success;
        status.error_msg = "";
        return status;
    } catch (std::exception& e) {
        auto status = CStatus();
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
        return status;
    }
}

CStatus
CreateIndexV2(CIndex* res_index, CBuildIndexInfo c_build_index_info) {
    try {
        auto build_index_info = (BuildIndexInfo*)c_build_index_info;
        auto field_type = build_index_info->field_type;
        milvus::index::CreateIndexInfo index_info;
        index_info.field_type = build_index_info->field_type;

        auto& config = build_index_info->config;
        // get index type
        auto index_type = milvus::index::GetValueFromConfig<std::string>(
            config, "index_type");
        AssertInfo(index_type.has_value(), "index type is empty");
        index_info.index_type = index_type.value();

        auto engine_version = build_index_info->index_engine_version;
        index_info.index_engine_version = engine_version;
        config[milvus::index::INDEX_ENGINE_VERSION] =
            std::to_string(engine_version);

        // get metric type
        if (milvus::datatype_is_vector(field_type)) {
            auto metric_type = milvus::index::GetValueFromConfig<std::string>(
                config, "metric_type");
            AssertInfo(metric_type.has_value(), "metric type is empty");
            index_info.metric_type = metric_type.value();
        }

        milvus::storage::FieldDataMeta field_meta{
            build_index_info->collection_id,
            build_index_info->partition_id,
            build_index_info->segment_id,
            build_index_info->field_id};
        milvus::storage::IndexMeta index_meta{
            build_index_info->segment_id,
            build_index_info->field_id,
            build_index_info->index_build_id,
            build_index_info->index_version,
            build_index_info->field_name,
            "",
            build_index_info->field_type,
            build_index_info->dim,
        };

        auto store_space = milvus_storage::Space::Open(
            build_index_info->data_store_path,
            milvus_storage::Options{nullptr,
                                    build_index_info->data_store_version});
        AssertInfo(store_space.ok() && store_space.has_value(),
                   fmt::format("create space failed: {}",
                               store_space.status().ToString()));

        auto index_space = milvus_storage::Space::Open(
            build_index_info->index_store_path,
            milvus_storage::Options{.schema = store_space.value()->schema()});
        AssertInfo(index_space.ok() && index_space.has_value(),
                   fmt::format("create space failed: {}",
                               index_space.status().ToString()));

        LOG_SEGCORE_INFO_ << "init space success";
        auto chunk_manager = milvus::storage::CreateChunkManager(
            build_index_info->storage_config);
        milvus::storage::FileManagerContext fileManagerContext(
            field_meta,
            index_meta,
            chunk_manager,
            std::move(index_space.value()));

        auto index =
            milvus::indexbuilder::IndexFactory::GetInstance().CreateIndex(
                build_index_info->field_type,
                build_index_info->field_name,
                config,
                fileManagerContext,
                std::move(store_space.value()));
        index->BuildV2();
        *res_index = index.release();
        return milvus::SuccessCStatus();
    } catch (std::exception& e) {
        return milvus::FailureCStatus(&e);
    }
}

CStatus
DeleteIndex(CIndex index) {
    auto status = CStatus();
    try {
        AssertInfo(index, "failed to delete index, passed index was null");
        auto cIndex =
            reinterpret_cast<milvus::indexbuilder::IndexCreatorBase*>(index);
        delete cIndex;
        status.error_code = Success;
        status.error_msg = "";
    } catch (std::exception& e) {
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
    }
    return status;
}

CStatus
BuildFloatVecIndex(CIndex index,
                   int64_t float_value_num,
                   const float* vectors) {
    auto status = CStatus();
    try {
        AssertInfo(index,
                   "failed to build float vector index, passed index was null");
        auto real_index =
            reinterpret_cast<milvus::indexbuilder::IndexCreatorBase*>(index);
        auto cIndex =
            dynamic_cast<milvus::indexbuilder::VecIndexCreator*>(real_index);
        auto dim = cIndex->dim();
        auto row_nums = float_value_num / dim;
        auto ds = knowhere::GenDataSet(row_nums, dim, vectors);
        cIndex->Build(ds);
        status.error_code = Success;
        status.error_msg = "";
    } catch (std::exception& e) {
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
    }
    return status;
}

CStatus
BuildBinaryVecIndex(CIndex index, int64_t data_size, const uint8_t* vectors) {
    auto status = CStatus();
    try {
        AssertInfo(
            index,
            "failed to build binary vector index, passed index was null");
        auto real_index =
            reinterpret_cast<milvus::indexbuilder::IndexCreatorBase*>(index);
        auto cIndex =
            dynamic_cast<milvus::indexbuilder::VecIndexCreator*>(real_index);
        auto dim = cIndex->dim();
        auto row_nums = (data_size * 8) / dim;
        auto ds = knowhere::GenDataSet(row_nums, dim, vectors);
        cIndex->Build(ds);
        status.error_code = Success;
        status.error_msg = "";
    } catch (std::exception& e) {
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
    }
    return status;
}

// field_data:
//  1, serialized proto::schema::BoolArray, if type is bool;
//  2, serialized proto::schema::StringArray, if type is string;
//  3, raw pointer, if type is of fundamental except bool type;
// TODO: optimize here if necessary.
CStatus
BuildScalarIndex(CIndex c_index, int64_t size, const void* field_data) {
    auto status = CStatus();
    try {
        AssertInfo(c_index,
                   "failed to build scalar index, passed index was null");

        auto real_index =
            reinterpret_cast<milvus::indexbuilder::IndexCreatorBase*>(c_index);
        const int64_t dim = 8;  // not important here
        auto dataset = knowhere::GenDataSet(size, dim, field_data);
        real_index->Build(dataset);

        status.error_code = Success;
        status.error_msg = "";
    } catch (std::exception& e) {
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
    }
    return status;
}

CStatus
SerializeIndexToBinarySet(CIndex index, CBinarySet* c_binary_set) {
    auto status = CStatus();
    try {
        AssertInfo(
            index,
            "failed to serialize index to binary set, passed index was null");
        auto real_index =
            reinterpret_cast<milvus::indexbuilder::IndexCreatorBase*>(index);
        auto binary =
            std::make_unique<knowhere::BinarySet>(real_index->Serialize());
        *c_binary_set = binary.release();
        status.error_code = Success;
        status.error_msg = "";
    } catch (std::exception& e) {
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
    }
    return status;
}

CStatus
LoadIndexFromBinarySet(CIndex index, CBinarySet c_binary_set) {
    auto status = CStatus();
    try {
        AssertInfo(
            index,
            "failed to load index from binary set, passed index was null");
        auto real_index =
            reinterpret_cast<milvus::indexbuilder::IndexCreatorBase*>(index);
        auto binary_set = reinterpret_cast<knowhere::BinarySet*>(c_binary_set);
        real_index->Load(*binary_set);
        status.error_code = Success;
        status.error_msg = "";
    } catch (std::exception& e) {
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
    }
    return status;
}

CStatus
CleanLocalData(CIndex index) {
    auto status = CStatus();
    try {
        AssertInfo(index,
                   "failed to build float vector index, passed index was null");
        auto real_index =
            reinterpret_cast<milvus::indexbuilder::IndexCreatorBase*>(index);
        auto cIndex =
            dynamic_cast<milvus::indexbuilder::VecIndexCreator*>(real_index);
        cIndex->CleanLocalData();
        status.error_code = Success;
        status.error_msg = "";
    } catch (std::exception& e) {
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
    }
    return status;
}

CStatus
NewBuildIndexInfo(CBuildIndexInfo* c_build_index_info,
                  CStorageConfig c_storage_config) {
    try {
        auto build_index_info = std::make_unique<BuildIndexInfo>();
        auto& storage_config = build_index_info->storage_config;
        storage_config.address = std::string(c_storage_config.address);
        storage_config.bucket_name = std::string(c_storage_config.bucket_name);
        storage_config.access_key_id =
            std::string(c_storage_config.access_key_id);
        storage_config.access_key_value =
            std::string(c_storage_config.access_key_value);
        storage_config.root_path = std::string(c_storage_config.root_path);
        storage_config.storage_type =
            std::string(c_storage_config.storage_type);
        storage_config.cloud_provider =
            std::string(c_storage_config.cloud_provider);
        storage_config.iam_endpoint =
            std::string(c_storage_config.iam_endpoint);
        storage_config.cloud_provider =
            std::string(c_storage_config.cloud_provider);
        storage_config.useSSL = c_storage_config.useSSL;
        storage_config.useIAM = c_storage_config.useIAM;
        storage_config.region = c_storage_config.region;
        storage_config.useVirtualHost = c_storage_config.useVirtualHost;
        storage_config.requestTimeoutMs = c_storage_config.requestTimeoutMs;

        *c_build_index_info = build_index_info.release();
        auto status = CStatus();
        status.error_code = Success;
        status.error_msg = "";
        return status;
    } catch (std::exception& e) {
        auto status = CStatus();
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
        return status;
    }
}

void
DeleteBuildIndexInfo(CBuildIndexInfo c_build_index_info) {
    auto info = (BuildIndexInfo*)c_build_index_info;
    delete info;
}

CStatus
AppendBuildIndexParam(CBuildIndexInfo c_build_index_info,
                      const uint8_t* serialized_index_params,
                      const uint64_t len) {
    try {
        auto build_index_info = (BuildIndexInfo*)c_build_index_info;
        auto index_params =
            std::make_unique<milvus::proto::indexcgo::IndexParams>();
        auto res = index_params->ParseFromArray(serialized_index_params, len);
        AssertInfo(res, "Unmarshall index params failed");
        for (auto i = 0; i < index_params->params_size(); ++i) {
            const auto& param = index_params->params(i);
            build_index_info->config[param.key()] = param.value();
        }

        auto status = CStatus();
        status.error_code = Success;
        status.error_msg = "";
        return status;
    } catch (std::exception& e) {
        auto status = CStatus();
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
        return status;
    }
}

CStatus
AppendBuildTypeParam(CBuildIndexInfo c_build_index_info,
                     const uint8_t* serialized_type_params,
                     const uint64_t len) {
    try {
        auto build_index_info = (BuildIndexInfo*)c_build_index_info;
        auto type_params =
            std::make_unique<milvus::proto::indexcgo::TypeParams>();
        auto res = type_params->ParseFromArray(serialized_type_params, len);
        AssertInfo(res, "Unmarshall index build type params failed");
        for (auto i = 0; i < type_params->params_size(); ++i) {
            const auto& param = type_params->params(i);
            build_index_info->config[param.key()] = param.value();
        }

        auto status = CStatus();
        status.error_code = Success;
        status.error_msg = "";
        return status;
    } catch (std::exception& e) {
        auto status = CStatus();
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
        return status;
    }
}

CStatus
AppendFieldMetaInfoV2(CBuildIndexInfo c_build_index_info,
                      int64_t collection_id,
                      int64_t partition_id,
                      int64_t segment_id,
                      int64_t field_id,
                      const char* field_name,
                      enum CDataType field_type,
                      int64_t dim) {
    try {
        auto build_index_info = (BuildIndexInfo*)c_build_index_info;
        build_index_info->collection_id = collection_id;
        build_index_info->partition_id = partition_id;
        build_index_info->segment_id = segment_id;
        build_index_info->field_id = field_id;
        build_index_info->field_type = milvus::DataType(field_type);
        build_index_info->field_name = field_name;
        build_index_info->dim = dim;

        return milvus::SuccessCStatus();
    } catch (std::exception& e) {
        return milvus::FailureCStatus(&e);
    }
}

CStatus
AppendFieldMetaInfo(CBuildIndexInfo c_build_index_info,
                    int64_t collection_id,
                    int64_t partition_id,
                    int64_t segment_id,
                    int64_t field_id,
                    enum CDataType field_type) {
    try {
        auto build_index_info = (BuildIndexInfo*)c_build_index_info;
        build_index_info->collection_id = collection_id;
        build_index_info->partition_id = partition_id;
        build_index_info->segment_id = segment_id;
        build_index_info->field_id = field_id;

        build_index_info->field_type = milvus::DataType(field_type);

        auto status = CStatus();
        status.error_code = Success;
        status.error_msg = "";
        return status;
    } catch (std::exception& e) {
        auto status = CStatus();
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
        return status;
    }
}

CStatus
AppendIndexMetaInfo(CBuildIndexInfo c_build_index_info,
                    int64_t index_id,
                    int64_t build_id,
                    int64_t version) {
    try {
        auto build_index_info = (BuildIndexInfo*)c_build_index_info;
        build_index_info->index_id = index_id;
        build_index_info->index_build_id = build_id;
        build_index_info->index_version = version;

        auto status = CStatus();
        status.error_code = Success;
        status.error_msg = "";
        return status;
    } catch (std::exception& e) {
        auto status = CStatus();
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
        return status;
    }
}

CStatus
AppendInsertFilePath(CBuildIndexInfo c_build_index_info,
                     const char* c_file_path) {
    try {
        auto build_index_info = (BuildIndexInfo*)c_build_index_info;
        std::string insert_file_path(c_file_path);
        build_index_info->insert_files.emplace_back(insert_file_path);

        auto status = CStatus();
        status.error_code = Success;
        status.error_msg = "";
        return status;
    } catch (std::exception& e) {
        return milvus::FailureCStatus(&e);
    }
}

CStatus
AppendIndexEngineVersionToBuildInfo(CBuildIndexInfo c_load_index_info,
                                    int32_t index_engine_version) {
    try {
        auto build_index_info = (BuildIndexInfo*)c_load_index_info;
        build_index_info->index_engine_version = index_engine_version;

        auto status = CStatus();
        status.error_code = Success;
        status.error_msg = "";
        return status;
    } catch (std::exception& e) {
        return milvus::FailureCStatus(&e);
    }
}

CStatus
AppendIndexStorageInfo(CBuildIndexInfo c_build_index_info,
                       const char* c_data_store_path,
                       const char* c_index_store_path,
                       int64_t data_store_version) {
    try {
        auto build_index_info = (BuildIndexInfo*)c_build_index_info;
        std::string data_store_path(c_data_store_path),
            index_store_path(c_index_store_path);
        build_index_info->data_store_path = data_store_path;
        build_index_info->index_store_path = index_store_path;
        build_index_info->data_store_version = data_store_version;

        auto status = CStatus();
        status.error_code = Success;
        status.error_msg = "";
        return status;
    } catch (std::exception& e) {
        auto status = CStatus();
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
        return status;
    }
}

CStatus
SerializeIndexAndUpLoad(CIndex index, CBinarySet* c_binary_set) {
    auto status = CStatus();
    try {
        AssertInfo(
            index,
            "failed to serialize index to binary set, passed index was null");
        auto real_index =
            reinterpret_cast<milvus::indexbuilder::IndexCreatorBase*>(index);
        auto binary =
            std::make_unique<knowhere::BinarySet>(real_index->Upload());
        *c_binary_set = binary.release();
        status.error_code = Success;
        status.error_msg = "";
    } catch (std::exception& e) {
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
    }
    return status;
}

CStatus
SerializeIndexAndUpLoadV2(CIndex index, CBinarySet* c_binary_set) {
    auto status = CStatus();
    try {
        AssertInfo(
            index,
            "failed to serialize index to binary set, passed index was null");

        auto real_index =
            reinterpret_cast<milvus::indexbuilder::IndexCreatorBase*>(index);

        auto binary =
            std::make_unique<knowhere::BinarySet>(real_index->UploadV2());
        *c_binary_set = binary.release();
        status.error_code = Success;
        status.error_msg = "";
    } catch (std::exception& e) {
        status.error_code = UnexpectedError;
        status.error_msg = strdup(e.what());
    }
    return status;
}
