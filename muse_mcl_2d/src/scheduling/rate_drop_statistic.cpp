#include <muse_mcl_2d/scheduling/scheduler_2d.hpp>

#include <cslibs_plugins/plugin.hpp>

#include <unordered_map>

#include <fstream>

namespace muse_mcl_2d {
class RateDropStatistic : public muse_mcl_2d::Scheduler2D
{
public:
    using Ptr                 = std::shared_ptr<RateDropStatistic>;
    using rate_t              = cslibs_time::Rate;
    using update_t            = muse_smc::Update<StateSpaceDescription2D, cslibs_plugins_data::Data>;
    using time_priority_map_t = std::unordered_map<id_t, double>;
    using resampling_t        = muse_smc::Resampling<StateSpaceDescription2D, cslibs_plugins_data::Data>;
    using sample_set_t        = muse_smc::SampleSet<StateSpaceDescription2D>;
    using count_map_t         = std::unordered_map<id_t, std::size_t>;
    using name_map_t          = std::unordered_map<id_t, std::string>;
    using time_t              = cslibs_time::Time;
    using duration_t          = cslibs_time::Duration;
    using update_model_map_t  = std::map<std::string, UpdateModel2D::Ptr>;


    virtual ~RateDropStatistic()
    {
        std::ofstream out(output_path_);
        for(auto &name : names_) {
            out << name.second << ": \n";
            out << "\t dropped   : " << drops_[name.first] << "\n";
            out << "\t procesed  : " << processed_[name.first] << "\n";
            out << "\t all-in-all: " << drops_[name.first] + processed_[name.first] << "\n";
            out << "\n";
        }
        out.flush();
        out.close();
    }


    inline void setup(const update_model_map_t &update_models,
                      ros::NodeHandle &nh) override
    {
        auto param_name = [this](const std::string &name){return name_ + "/" + name;};
        double preferred_RateDropStatistic = nh.param<double>(param_name("preferred_RateDropStatistic"), 5.0);
        resampling_period_ = duration_t(preferred_RateDropStatistic > 0.0 ? 1.0 / preferred_RateDropStatistic : 0.0);

        output_path_ = nh.param<std::string>("output_path", "/tmp/drop_statistic");

        for(const auto &um : update_models) {
            const UpdateModel2D::Ptr &u = um.second;
            const std::size_t id = u->getId();
            const std::string name = u->getName();
            drops_[id]      = 0;
            processed_[id]  = 0;
            names_[id]      = name;
        }
    }

    virtual bool apply(typename update_t::Ptr     &u,
                       typename sample_set_t::Ptr &s) override
    {
        auto now = []()
        {
            return time_t(ros::Time::now().toNSec());
        };

        if(last_update_time_.isZero())
            last_update_time_ = now();

        const time_t stamp = u->getStamp();
        if(stamp >= next_update_time_) {
            const time_t start = now();
            u->apply(s->getWeightIterator());
            const duration_t dur = (now() - start);
            const duration_t dur_prediction_resampling = (now() - last_update_time_);

            next_update_time_ = stamp + dur + dur_prediction_resampling;
            ++processed_[u->getModelId()];

            return true;
        }

        ++drops_[u->getModelId()];
        return false;
    }


    virtual bool apply(typename resampling_t::Ptr &r,
                       typename sample_set_t::Ptr &s) override
    {
        const cslibs_time::Time &stamp = s->getStamp();

        auto now = []()
        {
            return time_t(ros::Time::now().toNSec());
        };

        if(resampling_time_.isZero())
            resampling_time_ = stamp;

        auto do_apply = [&stamp, &r, &s, &now, this] () {
            r->apply(*s);

            resampling_time_   = stamp + resampling_period_;
            return true;
        };
        auto do_not_apply = [] () {
            return false;
        };
        return resampling_time_ < stamp ? do_apply() : do_not_apply();
    }

protected:
    time_t              next_update_time_;
    time_t              last_update_time_;
    time_t              resampling_time_;
    duration_t          resampling_period_;

    /// drop statistic stuff
    std::string         output_path_;
    count_map_t         drops_;
    count_map_t         processed_;
    name_map_t          names_;
};
}

CLASS_LOADER_REGISTER_CLASS(muse_mcl_2d::RateDropStatistic, muse_mcl_2d::Scheduler2D)
