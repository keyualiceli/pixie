package testingutils

import (
	"fmt"
	"testing"
	"time"

	"github.com/coreos/etcd/clientv3"
	"github.com/ory/dockertest"
	"github.com/sirupsen/logrus"
)

// SetupEtcd starts up an embedded etcd server on some free ports.
func SetupEtcd(t *testing.T) (*clientv3.Client, func()) {
	pool, err := dockertest.NewPool("")
	if err != nil {
		t.Fatalf("Could not connect to docker: %s", err)
	}
	resource, err := pool.RunWithOptions(&dockertest.RunOptions{
		Repository: "quay.io/coreos/etcd",
		Tag:        "v3.3.18",
		// It's safe to hardcode these ports because they are local to the Docker environment.
		Cmd: []string{"/usr/local/bin/etcd",
			"--data-dir=/etcd-data",
			"--name=node1",
			"--initial-advertise-peer-urls=http://0.0.0.0:2380",
			"--listen-peer-urls=http://0.0.0.0:2380",
			"--advertise-client-urls=http://0.0.0.0:2379",
			"--listen-client-urls=http://0.0.0.0:2379",
			"--initial-cluster=node1=http://0.0.0.0:2380",
		},
	})

	clientPort := resource.GetPort("2379/tcp")
	if err != nil {
		t.Fatal(err)
	}

	var client *clientv3.Client
	if err = pool.Retry(func() (err error) {
		client, err = clientv3.New(clientv3.Config{
			Endpoints:   []string{fmt.Sprintf("http://localhost:%s", clientPort)},
			DialTimeout: 5 * time.Second,
		})
		if err != nil {
			logrus.Errorf("Failed to connect to etcd: #{err}")
		}
		return err
	}); err != nil {
		t.Fatal("Cannot start etcd")
	}

	cleanup := func() {
		client.Close()
		if err := pool.Purge(resource); err != nil {
			t.Fatalf("Could not purge resource: %s", err)
		}
		client.Close()
	}

	return client, cleanup
}
