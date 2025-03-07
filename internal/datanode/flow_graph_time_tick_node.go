// Licensed to the LF AI & Data foundation under one
// or more contributor license agreements. See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership. The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package datanode

import (
	"fmt"
	"reflect"
	"time"

	"go.uber.org/atomic"
	"go.uber.org/zap"

	"github.com/milvus-io/milvus-proto/go-api/v2/msgpb"
	"github.com/milvus-io/milvus/internal/datanode/metacache"
	"github.com/milvus-io/milvus/internal/datanode/writebuffer"
	"github.com/milvus-io/milvus/internal/util/flowgraph"
	"github.com/milvus-io/milvus/pkg/log"
	"github.com/milvus-io/milvus/pkg/util/tsoutil"
)

// make sure ttNode implements flowgraph.Node
var _ flowgraph.Node = (*ttNode)(nil)

type ttNode struct {
	BaseNode
	vChannelName       string
	metacache          metacache.MetaCache
	writeBufferManager writebuffer.BufferManager
	lastUpdateTime     *atomic.Time
	cpUpdater          *channelCheckpointUpdater
}

// Name returns node name, implementing flowgraph.Node
func (ttn *ttNode) Name() string {
	return fmt.Sprintf("ttNode-%s", ttn.vChannelName)
}

func (ttn *ttNode) IsValidInMsg(in []Msg) bool {
	if !ttn.BaseNode.IsValidInMsg(in) {
		return false
	}
	_, ok := in[0].(*flowGraphMsg)
	if !ok {
		log.Warn("type assertion failed for flowGraphMsg", zap.String("name", reflect.TypeOf(in[0]).Name()))
		return false
	}
	return true
}

func (ttn *ttNode) Close() {
}

// Operate handles input messages, implementing flowgraph.Node
func (ttn *ttNode) Operate(in []Msg) []Msg {
	fgMsg := in[0].(*flowGraphMsg)
	curTs, _ := tsoutil.ParseTS(fgMsg.timeRange.timestampMax)
	if fgMsg.IsCloseMsg() {
		if len(fgMsg.endPositions) > 0 {
			channelPos, _, err := ttn.writeBufferManager.GetCheckpoint(ttn.vChannelName)
			if err != nil {
				log.Warn("channel removed", zap.String("channel", ttn.vChannelName), zap.Error(err))
				return []Msg{}
			}
			log.Info("flowgraph is closing, force update channel CP",
				zap.Time("cpTs", tsoutil.PhysicalTime(channelPos.GetTimestamp())),
				zap.String("channel", channelPos.GetChannelName()))
			ttn.updateChannelCP(channelPos, curTs)
		}
		return in
	}

	// Do not block and async updateCheckPoint
	channelPos, needUpdate, err := ttn.writeBufferManager.GetCheckpoint(ttn.vChannelName)
	if err != nil {
		log.Warn("channel removed", zap.String("channel", ttn.vChannelName), zap.Error(err))
		return []Msg{}
	}
	nonBlockingNotify := func() {
		ttn.updateChannelCP(channelPos, curTs)
	}

	if needUpdate || curTs.Sub(ttn.lastUpdateTime.Load()) >= updateChanCPInterval {
		nonBlockingNotify()
		return []Msg{}
	}

	return []Msg{}
}

func (ttn *ttNode) updateChannelCP(channelPos *msgpb.MsgPosition, curTs time.Time) error {
	callBack := func() error {
		channelCPTs, _ := tsoutil.ParseTS(channelPos.GetTimestamp())
		ttn.lastUpdateTime.Store(curTs)
		ttn.writeBufferManager.NotifyCheckpointUpdated(ttn.vChannelName, channelPos.GetTimestamp())
		log.Debug("UpdateChannelCheckpoint success",
			zap.String("channel", ttn.vChannelName),
			zap.Uint64("cpTs", channelPos.GetTimestamp()),
			zap.Time("cpTime", channelCPTs))
		return nil
	}

	err := ttn.cpUpdater.updateChannelCP(channelPos, callBack)
	return err
}

func newTTNode(config *nodeConfig, wbManager writebuffer.BufferManager, cpUpdater *channelCheckpointUpdater) (*ttNode, error) {
	baseNode := BaseNode{}
	baseNode.SetMaxQueueLength(Params.DataNodeCfg.FlowGraphMaxQueueLength.GetAsInt32())
	baseNode.SetMaxParallelism(Params.DataNodeCfg.FlowGraphMaxParallelism.GetAsInt32())

	tt := &ttNode{
		BaseNode:           baseNode,
		vChannelName:       config.vChannelName,
		metacache:          config.metacache,
		writeBufferManager: wbManager,
		lastUpdateTime:     atomic.NewTime(time.Time{}), // set to Zero to update channel checkpoint immediately after fg started
		cpUpdater:          cpUpdater,
	}

	return tt, nil
}
